/*
 * Copyright (C) 2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef WebVideoFullscreenModelVideoElement_h
#define WebVideoFullscreenModelVideoElement_h

#if PLATFORM(IOS) || (PLATFORM(MAC) && ENABLE(VIDEO_PRESENTATION_MODE))

#include "EventListener.h"
#include "FloatRect.h"
#include "HTMLMediaElementEnums.h"
#include "PlatformLayer.h"
#include "WebVideoFullscreenModel.h"
#include <functional>
#include <wtf/HashSet.h>
#include <wtf/RefPtr.h>
#include <wtf/RetainPtr.h>
#include <wtf/Vector.h>

namespace WebCore {
class AudioTrack;
class HTMLVideoElement;
class TextTrack;
class WebPlaybackSessionModelMediaElement;

class WebVideoFullscreenModelVideoElement : public WebVideoFullscreenModel, public EventListener {
public:
    static RefPtr<WebVideoFullscreenModelVideoElement> create()
    {
        return adoptRef(*new WebVideoFullscreenModelVideoElement());
    }
    WEBCORE_EXPORT virtual ~WebVideoFullscreenModelVideoElement();
    WEBCORE_EXPORT void setVideoElement(HTMLVideoElement*);
    HTMLVideoElement* videoElement() const { return m_videoElement.get(); }
    WEBCORE_EXPORT void setVideoFullscreenLayer(PlatformLayer*, std::function<void()> completionHandler = [] { });
    WEBCORE_EXPORT void waitForPreparedForInlineThen(std::function<void()> completionHandler = [] { });
    
    WEBCORE_EXPORT void handleEvent(WebCore::ScriptExecutionContext*, WebCore::Event*) override;
    void updateForEventName(const WTF::AtomicString&);
    bool operator==(const EventListener& rhs) const override { return static_cast<const WebCore::EventListener*>(this) == &rhs; }

    WEBCORE_EXPORT void addClient(WebVideoFullscreenModelClient&) override;
    WEBCORE_EXPORT void removeClient(WebVideoFullscreenModelClient&) override;
    WEBCORE_EXPORT void requestFullscreenMode(HTMLMediaElementEnums::VideoFullscreenMode) override;
    WEBCORE_EXPORT void setVideoLayerFrame(FloatRect) override;
    WEBCORE_EXPORT void setVideoLayerGravity(VideoGravity) override;
    WEBCORE_EXPORT void fullscreenModeChanged(HTMLMediaElementEnums::VideoFullscreenMode) override;
    WEBCORE_EXPORT bool isVisible() const override;
    WEBCORE_EXPORT FloatSize videoDimensions() const override { return m_videoDimensions; }
    WEBCORE_EXPORT bool hasVideo() const override { return m_hasVideo; }


protected:
    WEBCORE_EXPORT WebVideoFullscreenModelVideoElement();

private:
    void setHasVideo(bool);
    void setVideoDimensions(const FloatSize&);

    static const Vector<WTF::AtomicString>& observedEventNames();
    const WTF::AtomicString& eventNameAll();

    RefPtr<HTMLVideoElement> m_videoElement;
    RetainPtr<PlatformLayer> m_videoFullscreenLayer;
    bool m_isListening { false };
    HashSet<WebVideoFullscreenModelClient*> m_clients;
    bool m_hasVideo { false };
    FloatSize m_videoDimensions;
    FloatRect m_videoFrame;
    Vector<RefPtr<TextTrack>> m_legibleTracksForMenu;
    Vector<RefPtr<AudioTrack>> m_audioTracksForMenu;
};

}

#endif

#endif
