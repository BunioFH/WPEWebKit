/*
 *  Copyright (C) 2009, 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  Copyright (C) 2013 Collabora Ltd.
 *  Copyright (C) 2013 Orange
 *  Copyright (C) 2014, 2015 Sebastian Dröge <sebastian@centricular.com>
 *  Copyright (C) 2015, 2016 Metrological Group B.V.
 *  Copyright (C) 2015, 2016 Igalia, S.L
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "WebKitMediaSourceGStreamer.h"

#include "PlaybackPipeline.h"

#if ENABLE(VIDEO) && ENABLE(MEDIA_SOURCE) && USE(GSTREAMER)

#include "AudioTrackPrivateGStreamer.h"
#include "GStreamerUtilities.h"
#include "MediaDescription.h"
#include "MediaPlayerPrivateGStreamerMSE.h"
#include "MediaSample.h"
#include "MediaSourceGStreamer.h"
#include "NotImplemented.h"
#include "SourceBufferPrivateGStreamer.h"
#include "TimeRanges.h"
#include "VideoTrackPrivateGStreamer.h"
#include "WebKitMediaSourceGStreamerPrivate.h"

#include <gst/app/app.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/pbutils/missing-plugins.h>
#include <gst/pbutils/pbutils.h>
#include <gst/video/video.h>
#include <wtf/Condition.h>
#include <wtf/MainThread.h>
#include <wtf/glib/GMutexLocker.h>
#include <wtf/glib/GUniquePtr.h>
#include <wtf/text/CString.h>

GST_DEBUG_CATEGORY_STATIC(webkit_media_src_debug);
#define GST_CAT_DEFAULT webkit_media_src_debug

#define webkit_media_src_parent_class parent_class
#define WEBKIT_MEDIA_SRC_CATEGORY_INIT GST_DEBUG_CATEGORY_INIT(webkit_media_src_debug, "webkitmediasrc", 0, "websrc element");

static GstStaticPadTemplate srcTemplate = GST_STATIC_PAD_TEMPLATE("src_%u", GST_PAD_SRC,
    GST_PAD_SOMETIMES, GST_STATIC_CAPS_ANY);

static void enabledAppsrcNeedData(GstAppSrc*, guint, gpointer);
static void enabledAppsrcEnoughData(GstAppSrc*, gpointer);
static gboolean enabledAppsrcSeekData(GstAppSrc*, guint64, gpointer);

static void disabledAppsrcNeedData(GstAppSrc*, guint, gpointer) { };
static void disabledAppsrcEnoughData(GstAppSrc*, gpointer) { };
static gboolean disabledAppsrcSeekData(GstAppSrc*, guint64, gpointer)
{
    return FALSE;
};

GstAppSrcCallbacks enabledAppsrcCallbacks = {
    enabledAppsrcNeedData,
    enabledAppsrcEnoughData,
    enabledAppsrcSeekData,
    { 0 }
};

GstAppSrcCallbacks disabledAppsrcCallbacks = {
    disabledAppsrcNeedData,
    disabledAppsrcEnoughData,
    disabledAppsrcSeekData,
    { 0 }
};

static Stream* getStreamByAppsrc(WebKitMediaSrc*, GstElement*);

static void enabledAppsrcNeedData(GstAppSrc* appsrc, guint, gpointer userData)
{
    WebKitMediaSrc* webKitMediaSrc = static_cast<WebKitMediaSrc*>(userData);
    ASSERT(WEBKIT_IS_MEDIA_SRC(webKitMediaSrc));

    GST_OBJECT_LOCK(webKitMediaSrc);
    OnSeekDataAction appsrcSeekDataNextAction = webKitMediaSrc->priv->appsrcSeekDataNextAction;
    Stream* appsrcStream = getStreamByAppsrc(webKitMediaSrc, GST_ELEMENT(appsrc));
    bool allAppsrcNeedDataAfterSeek = false;

    if (webKitMediaSrc->priv->appsrcSeekDataCount > 0) {
        if (appsrcStream && !appsrcStream->appsrcNeedDataFlag) {
            ++webKitMediaSrc->priv->appsrcNeedDataCount;
            appsrcStream->appsrcNeedDataFlag = true;
        }
        int numAppsrcs = webKitMediaSrc->priv->streams.size();
        if (webKitMediaSrc->priv->appsrcSeekDataCount == numAppsrcs && webKitMediaSrc->priv->appsrcNeedDataCount == numAppsrcs) {
            GST_DEBUG("All needDatas completed");
            allAppsrcNeedDataAfterSeek = true;
            webKitMediaSrc->priv->appsrcSeekDataCount = 0;
            webKitMediaSrc->priv->appsrcNeedDataCount = 0;
            webKitMediaSrc->priv->appsrcSeekDataNextAction = Nothing;

            for (Stream* stream : webKitMediaSrc->priv->streams)
                stream->appsrcNeedDataFlag = false;
        }
    }
    GST_OBJECT_UNLOCK(webKitMediaSrc);

    if (allAppsrcNeedDataAfterSeek) {
        GST_DEBUG("All expected appsrcSeekData() and appsrcNeedData() calls performed. Running next action (%d)", static_cast<int>(appsrcSeekDataNextAction));

        switch (appsrcSeekDataNextAction) {
        case MediaSourceSeekToTime: {
            GstStructure* structure = gst_structure_new_empty("seek-needs-data");
            GstMessage* message = gst_message_new_application(GST_OBJECT(appsrc), structure);
            gst_bus_post(webKitMediaSrc->priv->bus.get(), message);
            GST_TRACE("seek-needs-data message posted to the bus");
            break;
        }
        case Nothing:
            break;
        }
    } else if (appsrcSeekDataNextAction == Nothing) {
        LockHolder locker(webKitMediaSrc->priv->streamLock);

        GST_OBJECT_LOCK(webKitMediaSrc);

        // Search again for the Stream, just in case it was removed between the previous lock and this one.
        appsrcStream = getStreamByAppsrc(webKitMediaSrc, GST_ELEMENT(appsrc));

        if (appsrcStream && appsrcStream->type != WebCore::Invalid) {
            GstStructure* structure = gst_structure_new("ready-for-more-samples", "appsrc-stream", G_TYPE_POINTER, appsrcStream, nullptr);
            GstMessage* message = gst_message_new_application(GST_OBJECT(appsrc), structure);
            gst_bus_post(webKitMediaSrc->priv->bus.get(), message);
            GST_TRACE("ready-for-more-samples message posted to the bus");
        }

        GST_OBJECT_UNLOCK(webKitMediaSrc);
    }
}

static void enabledAppsrcEnoughData(GstAppSrc *appsrc, gpointer userData)
{
    // No need to lock on webKitMediaSrc, we're on the main thread and nobody is going to remove the stream in the meantime.
    ASSERT(WTF::isMainThread());

    WebKitMediaSrc* webKitMediaSrc = static_cast<WebKitMediaSrc*>(userData);
    ASSERT(WEBKIT_IS_MEDIA_SRC(webKitMediaSrc));

    Stream* stream = getStreamByAppsrc(webKitMediaSrc, GST_ELEMENT(appsrc));

    // This callback might have been scheduled from a child thread before the stream was removed.
    // Then, the removal code might have run, and later this callback.
    // This check solves the race condition.
    if (!stream || stream->type == WebCore::Invalid)
        return;

    stream->sourceBuffer->setReadyForMoreSamples(false);
}

static gboolean enabledAppsrcSeekData(GstAppSrc*, guint64, gpointer userData)
{
    ASSERT(WTF::isMainThread());

    WebKitMediaSrc* webKitMediaSrc = static_cast<WebKitMediaSrc*>(userData);

    ASSERT(WEBKIT_IS_MEDIA_SRC(webKitMediaSrc));

    GST_OBJECT_LOCK(webKitMediaSrc);
    webKitMediaSrc->priv->appsrcSeekDataCount++;
    GST_OBJECT_UNLOCK(webKitMediaSrc);

    return TRUE;
}

static Stream* getStreamByAppsrc(WebKitMediaSrc* source, GstElement* appsrc)
{
    for (Stream* stream : source->priv->streams) {
        if (stream->appsrc == appsrc)
            return stream;
    }
    return nullptr;
}

G_DEFINE_TYPE_WITH_CODE(WebKitMediaSrc, webkit_media_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, webKitMediaSrcUriHandlerInit);
    WEBKIT_MEDIA_SRC_CATEGORY_INIT);

guint webKitMediaSrcSignals[LAST_SIGNAL] = { 0 };

static void webkit_media_src_class_init(WebKitMediaSrcClass* klass)
{
    GObjectClass* oklass = G_OBJECT_CLASS(klass);
    GstElementClass* eklass = GST_ELEMENT_CLASS(klass);

    oklass->finalize = webKitMediaSrcFinalize;
    oklass->set_property = webKitMediaSrcSetProperty;
    oklass->get_property = webKitMediaSrcGetProperty;

    gst_element_class_add_pad_template(eklass, gst_static_pad_template_get(&srcTemplate));

    gst_element_class_set_static_metadata(eklass, "WebKit Media source element", "Source", "Handles Blob uris", "Stephane Jadaud <sjadaud@sii.fr>, Sebastian Dröge <sebastian@centricular.com>, Enrique Ocaña González <eocanha@igalia.com>");

    // Allows setting the uri using the 'location' property, which is used for example by gst_element_make_from_uri().
    g_object_class_install_property(oklass,
        PROP_LOCATION,
        g_param_spec_string("location", "location", "Location to read from", nullptr,
        GParamFlags(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oklass,
        PROP_N_AUDIO,
        g_param_spec_int("n-audio", "Number Audio", "Total number of audio streams",
        0, G_MAXINT, 0, GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oklass,
        PROP_N_VIDEO,
        g_param_spec_int("n-video", "Number Video", "Total number of video streams",
        0, G_MAXINT, 0, GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(oklass,
        PROP_N_TEXT,
        g_param_spec_int("n-text", "Number Text", "Total number of text streams",
        0, G_MAXINT, 0, GParamFlags(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

    webKitMediaSrcSignals[SIGNAL_VIDEO_CHANGED] =
        g_signal_new("video-changed", G_TYPE_FROM_CLASS(oklass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitMediaSrcClass, videoChanged), nullptr, nullptr,
        g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
    webKitMediaSrcSignals[SIGNAL_AUDIO_CHANGED] =
        g_signal_new("audio-changed", G_TYPE_FROM_CLASS(oklass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitMediaSrcClass, audioChanged), nullptr, nullptr,
        g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);
    webKitMediaSrcSignals[SIGNAL_TEXT_CHANGED] =
        g_signal_new("text-changed", G_TYPE_FROM_CLASS(oklass),
        G_SIGNAL_RUN_LAST,
        G_STRUCT_OFFSET(WebKitMediaSrcClass, textChanged), nullptr, nullptr,
        g_cclosure_marshal_generic, G_TYPE_NONE, 0, G_TYPE_NONE);

    eklass->change_state = webKitMediaSrcChangeState;

    g_type_class_add_private(klass, sizeof(WebKitMediaSrcPrivate));
}

static void webkit_media_src_init(WebKitMediaSrc* source)
{
    source->priv = WEBKIT_MEDIA_SRC_GET_PRIVATE(source);
    new (source->priv) WebKitMediaSrcPrivate();
    source->priv->seekTime = MediaTime::invalidTime();
    source->priv->appsrcSeekDataCount = 0;
    source->priv->appsrcNeedDataCount = 0;
    source->priv->appsrcSeekDataNextAction = Nothing;

    // No need to reset Stream.appsrcNeedDataFlag because there are no Streams at this point yet.
}

void webKitMediaSrcFinalize(GObject* object)
{
    ASSERT(WTF::isMainThread());

    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(object);
    WebKitMediaSrcPrivate* priv = source->priv;

    Vector<Stream*> oldStreams;
    source->priv->streams.swap(oldStreams);

    for (Stream* stream : oldStreams)
        webKitMediaSrcFreeStream(source, stream);

    priv->seekTime = MediaTime::invalidTime();

    if (priv->mediaPlayerPrivate)
        webKitMediaSrcSetMediaPlayerPrivate(source, nullptr);

    // We used a placement new for construction, the destructor won't be called automatically.
    priv->~_WebKitMediaSrcPrivate();

    GST_CALL_PARENT(G_OBJECT_CLASS, finalize, (object));
}

void webKitMediaSrcSetProperty(GObject* object, guint propId, const GValue* value, GParamSpec* pspec)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(object);

    switch (propId) {
    case PROP_LOCATION:
        gst_uri_handler_set_uri(reinterpret_cast<GstURIHandler*>(source), g_value_get_string(value), nullptr);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
        break;
    }
}

void webKitMediaSrcGetProperty(GObject* object, guint propId, GValue* value, GParamSpec* pspec)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(object);
    WebKitMediaSrcPrivate* priv = source->priv;

    GST_OBJECT_LOCK(source);
    switch (propId) {
    case PROP_LOCATION:
        g_value_set_string(value, priv->location.get());
        break;
    case PROP_N_AUDIO:
        g_value_set_int(value, priv->numberOfAudioStreams);
        break;
    case PROP_N_VIDEO:
        g_value_set_int(value, priv->numberOfVideoStreams);
        break;
    case PROP_N_TEXT:
        g_value_set_int(value, priv->numberOfTextStreams);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, pspec);
        break;
    }
    GST_OBJECT_UNLOCK(source);
}

void webKitMediaSrcDoAsyncStart(WebKitMediaSrc* source)
{
    source->priv->asyncStart = true;
    GST_BIN_CLASS(parent_class)->handle_message(GST_BIN(source),
        gst_message_new_async_start(GST_OBJECT(source)));
}

void webKitMediaSrcDoAsyncDone(WebKitMediaSrc* source)
{
    WebKitMediaSrcPrivate* priv = source->priv;
    if (priv->asyncStart) {
        GST_BIN_CLASS(parent_class)->handle_message(GST_BIN(source),
            gst_message_new_async_done(GST_OBJECT(source), GST_CLOCK_TIME_NONE));
        priv->asyncStart = false;
    }
}

GstStateChangeReturn webKitMediaSrcChangeState(GstElement* element, GstStateChange transition)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(element);
    WebKitMediaSrcPrivate* priv = source->priv;

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        priv->allTracksConfigured = false;
        webKitMediaSrcDoAsyncStart(source);
        break;
    default:
        break;
    }

    GstStateChangeReturn result = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
    if (G_UNLIKELY(result == GST_STATE_CHANGE_FAILURE)) {
        GST_WARNING_OBJECT(source, "State change failed");
        webKitMediaSrcDoAsyncDone(source);
        return result;
    }

    switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
        result = GST_STATE_CHANGE_ASYNC;
        break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
        webKitMediaSrcDoAsyncDone(source);
        priv->allTracksConfigured = false;
        break;
    default:
        break;
    }

    return result;
}

gint64 webKitMediaSrcGetSize(WebKitMediaSrc* webKitMediaSrc)
{
    gint64 duration = 0;
    for (Stream* stream : webKitMediaSrc->priv->streams)
        duration = std::max<gint64>(duration, gst_app_src_get_size(GST_APP_SRC(stream->appsrc)));
    return duration;
}

gboolean webKitMediaSrcQueryWithParent(GstPad* pad, GstObject* parent, GstQuery* query)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(GST_ELEMENT(parent));
    gboolean result = FALSE;

    switch (GST_QUERY_TYPE(query)) {
    case GST_QUERY_DURATION: {
        GstFormat format;
        gst_query_parse_duration(query, &format, nullptr);

        GST_DEBUG_OBJECT(source, "duration query in format %s", gst_format_get_name(format));
        GST_OBJECT_LOCK(source);
        switch (format) {
        case GST_FORMAT_TIME: {
            if (source->priv && source->priv->mediaPlayerPrivate) {
                float duration = source->priv->mediaPlayerPrivate->durationMediaTime().toFloat();
                if (duration > 0) {
                    gst_query_set_duration(query, format, WebCore::toGstClockTime(duration));
                    GST_DEBUG_OBJECT(source, "Answering: duration=%" GST_TIME_FORMAT, GST_TIME_ARGS(WebCore::toGstClockTime(duration)));
                    result = TRUE;
                }
            }
            break;
        }
        case GST_FORMAT_BYTES: {
            if (source->priv) {
                gint64 duration = webKitMediaSrcGetSize(source);
                if (duration) {
                    gst_query_set_duration(query, format, duration);
                    GST_DEBUG_OBJECT(source, "size: %" G_GINT64_FORMAT, duration);
                    result = TRUE;
                }
            }
            break;
        }
        default:
            break;
        }

        GST_OBJECT_UNLOCK(source);
        break;
    }
    case GST_QUERY_URI:
        if (source) {
            GST_OBJECT_LOCK(source);
            if (source->priv)
                gst_query_set_uri(query, source->priv->location.get());
            GST_OBJECT_UNLOCK(source);
        }
        result = TRUE;
        break;
    default: {
        GRefPtr<GstPad> target = adoptGRef(gst_ghost_pad_get_target(GST_GHOST_PAD_CAST(pad)));
        // Forward the query to the proxy target pad.
        if (target)
            result = gst_pad_query(target.get(), query);
        break;
    }
    }

    return result;
}

void webKitMediaSrcUpdatePresentationSize(GstCaps* caps, Stream* stream)
{
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* structureName = gst_structure_get_name(structure);
    GstVideoInfo info;

    GST_OBJECT_LOCK(stream->parent);
    if (g_str_has_prefix(structureName, "video/") && gst_video_info_from_caps(&info, caps)) {
        float width, height;

        // FIXME: Correct?.
        width = info.width;
        height = info.height * ((float) info.par_d / (float) info.par_n);
        stream->presentationSize = WebCore::FloatSize(width, height);
    } else
        stream->presentationSize = WebCore::FloatSize();

    gst_caps_ref(caps);
    stream->caps = adoptGRef(caps);
    GST_OBJECT_UNLOCK(stream->parent);
}

void webKitMediaSrcLinkStreamToSrcPad(GstPad* sourcePad, Stream* stream)
{
    unsigned padId = static_cast<unsigned>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(sourcePad), "padId")));
    GST_DEBUG_OBJECT(stream->parent, "linking stream to src pad (id: %u)", padId);

    GUniquePtr<gchar> padName(g_strdup_printf("src_%u", padId));
    GstPad* ghostpad = WebCore::webkitGstGhostPadFromStaticTemplate(&srcTemplate, padName.get(), sourcePad);

    gst_pad_set_query_function(ghostpad, webKitMediaSrcQueryWithParent);

    gst_pad_set_active(ghostpad, TRUE);
    gst_element_add_pad(GST_ELEMENT(stream->parent), ghostpad);

    if (stream->decodebinSinkPad) {
        GST_DEBUG_OBJECT(stream->parent, "A decodebin was previously used for this source, trying to reuse it.");
        // FIXME: error checking here. Not sure what to do if linking
        // fails though, because decodebin is out of this source
        // element's scope, in theory.
        gst_pad_link(ghostpad, stream->decodebinSinkPad);
    }
}

void webKitMediaSrcLinkParser(GstPad* sourcePad, GstCaps* caps, Stream* stream)
{
    ASSERT(caps && stream->parent);
    if (!caps || !stream->parent) {
        GST_ERROR("Unable to link parser");
        return;
    }

    webKitMediaSrcUpdatePresentationSize(caps, stream);

    // FIXME: drop webKitMediaSrcLinkStreamToSrcPad() and move its code here.
    if (!gst_pad_is_linked(sourcePad)) {
        GST_DEBUG_OBJECT(stream->parent, "pad not linked yet");
        webKitMediaSrcLinkStreamToSrcPad(sourcePad, stream);
    }

    webKitMediaSrcCheckAllTracksConfigured(stream->parent);
}

void webKitMediaSrcFreeStream(WebKitMediaSrc* source, Stream* stream)
{
    if (stream->appsrc) {
        // Don't trigger callbacks from this appsrc to avoid using the stream anymore.
        gst_app_src_set_callbacks(GST_APP_SRC(stream->appsrc), &disabledAppsrcCallbacks, nullptr, nullptr);
        gst_app_src_end_of_stream(GST_APP_SRC(stream->appsrc));
    }

    if (stream->type != WebCore::Invalid) {
        GST_DEBUG("Freeing track-related info on stream %p", stream);

        LockHolder locker(source->priv->streamLock);

        if (stream->caps)
            stream->caps = nullptr;

        if (stream->audioTrack)
            stream->audioTrack = nullptr;
        if (stream->videoTrack)
            stream->videoTrack = nullptr;

        int signal = -1;
        switch (stream->type) {
        case WebCore::Audio:
            signal = SIGNAL_AUDIO_CHANGED;
            break;
        case WebCore::Video:
            signal = SIGNAL_VIDEO_CHANGED;
            break;
        case WebCore::Text:
            signal = SIGNAL_TEXT_CHANGED;
            break;
        default:
            break;
        }
        stream->type = WebCore::Invalid;

        if (signal != -1)
            g_signal_emit(G_OBJECT(source), webKitMediaSrcSignals[signal], 0, nullptr);

        source->priv->streamCondition.notifyOne();
    }

    GST_DEBUG("Releasing stream: %p", stream);
    delete stream;
}

void webKitMediaSrcCheckAllTracksConfigured(WebKitMediaSrc* webKitMediaSrc)
{
    bool allTracksConfigured = false;

    GST_OBJECT_LOCK(webKitMediaSrc);
    if (!webKitMediaSrc->priv->allTracksConfigured) {
        allTracksConfigured = true;
        for (Stream* stream : webKitMediaSrc->priv->streams) {
            if (stream->type == WebCore::Invalid) {
                allTracksConfigured = false;
                break;
            }
        }
        if (allTracksConfigured)
            webKitMediaSrc->priv->allTracksConfigured = true;
    }
    GST_OBJECT_UNLOCK(webKitMediaSrc);

    if (allTracksConfigured) {
        GST_DEBUG("All tracks attached. Completing async state change operation.");
        gst_element_no_more_pads(GST_ELEMENT(webKitMediaSrc));
        webKitMediaSrcDoAsyncDone(webKitMediaSrc);
    }
}

// Uri handler interface.
GstURIType webKitMediaSrcUriGetType(GType)
{
    return GST_URI_SRC;
}

const gchar* const* webKitMediaSrcGetProtocols(GType)
{
    static const char* protocols[] = {"mediasourceblob", 0 };
    return protocols;
}

gchar* webKitMediaSrcGetUri(GstURIHandler* handler)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(handler);
    gchar* result;

    GST_OBJECT_LOCK(source);
    result = g_strdup(source->priv->location.get());
    GST_OBJECT_UNLOCK(source);
    return result;
}

gboolean webKitMediaSrcSetUri(GstURIHandler* handler, const gchar* uri, GError**)
{
    WebKitMediaSrc* source = WEBKIT_MEDIA_SRC(handler);

    if (GST_STATE(source) >= GST_STATE_PAUSED) {
        GST_ERROR_OBJECT(source, "URI can only be set in states < PAUSED");
        return FALSE;
    }

    GST_OBJECT_LOCK(source);
    WebKitMediaSrcPrivate* priv = source->priv;
    priv->location = nullptr;
    if (!uri) {
        GST_OBJECT_UNLOCK(source);
        return TRUE;
    }

    WebCore::URL url(WebCore::URL(), uri);

    priv->location = GUniquePtr<gchar>(g_strdup(url.string().utf8().data()));
    GST_OBJECT_UNLOCK(source);
    return TRUE;
}

void webKitMediaSrcUriHandlerInit(gpointer gIface, gpointer)
{
    GstURIHandlerInterface* iface = (GstURIHandlerInterface *) gIface;

    iface->get_type = webKitMediaSrcUriGetType;
    iface->get_protocols = webKitMediaSrcGetProtocols;
    iface->get_uri = webKitMediaSrcGetUri;
    iface->set_uri = webKitMediaSrcSetUri;
}

static void seekNeedsDataMainThread(WebKitMediaSrc* source)
{
    GST_DEBUG("Buffering needed before seek");

    ASSERT(WTF::isMainThread());

    GST_OBJECT_LOCK(source);
    MediaTime seekTime = source->priv->seekTime;
    WebCore::MediaPlayerPrivateGStreamerMSE* mediaPlayerPrivate = source->priv->mediaPlayerPrivate;

    if (!mediaPlayerPrivate) {
        GST_OBJECT_UNLOCK(source);
        return;
    }

    for (Stream* stream : source->priv->streams) {
        if (stream->type != WebCore::Invalid)
            stream->sourceBuffer->setReadyForMoreSamples(true);
    }
    GST_OBJECT_UNLOCK(source);
    mediaPlayerPrivate->notifySeekNeedsDataForTime(seekTime);
}

static void notifyReadyForMoreSamplesMainThread(WebKitMediaSrc* source, Stream* appsrcStream)
{
    GST_OBJECT_LOCK(source);

    auto it = std::find(source->priv->streams.begin(), source->priv->streams.end(), appsrcStream);
    if (it == source->priv->streams.end()) {
        GST_OBJECT_UNLOCK(source);
        return;
    }

    WebCore::MediaPlayerPrivateGStreamerMSE* mediaPlayerPrivate = source->priv->mediaPlayerPrivate;
    if (mediaPlayerPrivate && !mediaPlayerPrivate->seeking())
        appsrcStream->sourceBuffer->notifyReadyForMoreSamples();

    GST_OBJECT_UNLOCK(source);
}

static void applicationMessageCallback(GstBus*, GstMessage* message, WebKitMediaSrc* source)
{
    ASSERT(WTF::isMainThread());
    ASSERT(GST_MESSAGE_TYPE(message) == GST_MESSAGE_APPLICATION);

    const GstStructure* structure = gst_message_get_structure(message);

    if (gst_structure_has_name(structure, "seek-needs-data")) {
        seekNeedsDataMainThread(source);
        return;
    }

    if (gst_structure_has_name(structure, "ready-for-more-samples")) {
        Stream* appsrcStream = nullptr;
        gst_structure_get(structure, "appsrc-stream", G_TYPE_POINTER, &appsrcStream, nullptr);
        ASSERT(appsrcStream);

        notifyReadyForMoreSamplesMainThread(source, appsrcStream);
        return;
    }

    ASSERT_NOT_REACHED();
}

void webKitMediaSrcSetMediaPlayerPrivate(WebKitMediaSrc* source, WebCore::MediaPlayerPrivateGStreamerMSE* mediaPlayerPrivate)
{
    GST_OBJECT_LOCK(source);
    if (source->priv->mediaPlayerPrivate && source->priv->mediaPlayerPrivate != mediaPlayerPrivate && source->priv->bus)
        g_signal_handlers_disconnect_by_func(source->priv->bus.get(), gpointer(applicationMessageCallback), source);

    // Set to nullptr on MediaPlayerPrivateGStreamer destruction, never a dangling pointer.
    source->priv->mediaPlayerPrivate = mediaPlayerPrivate;
    source->priv->bus = mediaPlayerPrivate ? adoptGRef(gst_pipeline_get_bus(GST_PIPELINE(mediaPlayerPrivate->pipeline()))) : nullptr;
    if (source->priv->bus) {
        // MediaPlayerPrivateGStreamer has called gst_bus_add_signal_watch() at this point, so we can subscribe.
        g_signal_connect(source->priv->bus.get(), "message::application", G_CALLBACK(applicationMessageCallback), source);
    }
    GST_OBJECT_UNLOCK(source);
}

void webKitMediaSrcSetReadyForSamples(WebKitMediaSrc* source, bool isReady)
{
    if (source) {
        GST_OBJECT_LOCK(source);
        for (Stream* stream : source->priv->streams)
            stream->sourceBuffer->setReadyForMoreSamples(isReady);
        GST_OBJECT_UNLOCK(source);
    }
}

void webKitMediaSrcPrepareSeek(WebKitMediaSrc* source, const MediaTime& time)
{
    GST_OBJECT_LOCK(source);
    source->priv->seekTime = time;
    source->priv->appsrcSeekDataCount = 0;
    source->priv->appsrcNeedDataCount = 0;

    for (Stream* stream : source->priv->streams) {
        stream->appsrcNeedDataFlag = false;
        // Don't allow samples away from the seekTime to be enqueued.
        stream->lastEnqueuedTime = time;
    }

    // The pending action will be performed in enabledAppsrcSeekData().
    source->priv->appsrcSeekDataNextAction = MediaSourceSeekToTime;
    GST_OBJECT_UNLOCK(source);
}

namespace WTF {
template <> GRefPtr<WebKitMediaSrc> adoptGRef(WebKitMediaSrc* ptr)
{
    ASSERT(!ptr || !g_object_is_floating(G_OBJECT(ptr)));
    return GRefPtr<WebKitMediaSrc>(ptr, GRefPtrAdopt);
}

template <> WebKitMediaSrc* refGPtr<WebKitMediaSrc>(WebKitMediaSrc* ptr)
{
    if (ptr)
        gst_object_ref_sink(GST_OBJECT(ptr));

    return ptr;
}

template <> void derefGPtr<WebKitMediaSrc>(WebKitMediaSrc* ptr)
{
    if (ptr)
        gst_object_unref(ptr);
}
};

#endif // USE(GSTREAMER)

