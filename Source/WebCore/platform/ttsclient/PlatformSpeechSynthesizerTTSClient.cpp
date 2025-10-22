/* Copyright (C) 2019 RDK Management.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDERS. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(SPEECH_SYNTHESIS) && USE(TTS_CLIENT)
#include "PlatformSpeechSynthesizer.h"
#include "PlatformSpeechSynthesisUtterance.h"

#include "TTSClient.h"

#include <wtf/WeakPtr.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_ALLOWED_TEXT_LENGTH 1024

#define CHECK_TTS_SESSION(utterance)                                                    \
    do {                                                                                \
        if (!m_ttsConnected) {                                                          \
            notifyClient(utterance, SpeechSynthesisErrorCode::SynthesisUnavailable);    \
            return;                                                                     \
        }                                                                               \
        if (!m_ttsClient || !m_ttsSessionId) {                                          \
            notifyClient(utterance, SpeechSynthesisErrorCode::NotAllowed);              \
            return;                                                                     \
        }                                                                               \
    } while (0)

namespace {
static bool bSpeechSynthOverrideSysTTSConfig = getenv("SPEECH_SYNTHESIS_OVERRIDE_SYSTEM_TTS_CONFIG");
} // namespace

namespace WebCore {

class TTSClientPlatformSpeechSynthesizerWrapper : public TTS::TTSConnectionCallback,
                                                  public TTS::TTSSessionCallback,
                                                  public CanMakeWeakPtr<TTSClientPlatformSpeechSynthesizerWrapper>,
                                                  public RefCounted<TTSClientPlatformSpeechSynthesizerWrapper> {
public:
    TTSClientPlatformSpeechSynthesizerWrapper(PlatformSpeechSynthesizer& platformSynthesizer)
        : m_platformSynthesizer(platformSynthesizer)
        , m_shouldCacheUtterance(true)
        , m_ttsSessionId(0)
        , m_ttsConnected(false)
        , m_currentUtteranceSpeechId(0)
    {
        m_ttsClient = TTS::TTSClient::create(this);

        auto ttsConnectionTO = 10_s;
        RunLoop::main().dispatchAfter(ttsConnectionTO, [weakThis = m_weakPtrFactory.createWeakPtr(*this)]() {
            if (!weakThis)
                return;

            weakThis->m_shouldCacheUtterance = false;
            if (weakThis->m_firstUtterance.get()) {
                if (weakThis->m_ttsSessionId == 0)
                    weakThis->notifyClient(weakThis->m_firstUtterance, SpeechSynthesisErrorCode::SynthesisUnavailable);
                weakThis->m_firstUtterance = nullptr;
            }
        });
    }

    ~TTSClientPlatformSpeechSynthesizerWrapper()
    {
        if (m_ttsClient) {
            m_ttsSessionId = 0;
            delete m_ttsClient;
        }
    }

    // Delegated functions called by PlatformSpeechSynthesizer

    void fillVoiceList(Vector<RefPtr<PlatformSpeechSynthesisVoice>>& voiceList)
    {
        if (m_ttsClient) {
            const char* v = nullptr;
            TTS::Configuration config;
            std::vector<std::string> voices;

            TTS::TTS_Error err = m_ttsClient->getTTSConfiguration(config);
            m_ttsClient->listVoices(config.language, voices);
            for (unsigned int i = 0; i < voices.size(); i++) {
                v = voices[i].c_str();
                voiceList.append(PlatformSpeechSynthesisVoice::create(
                    String::fromUTF8(v),
                    String::fromUTF8(v),
                    String::fromUTF8((err == TTS::TTS_OK) ? config.language.c_str() : "en-US"),
                    true,
                    true));
            }

            m_TTSVolume = config.volume;
            m_TTSRate = config.rate;
        }
    }

    void speak(RefPtr<PlatformSpeechSynthesisUtterance>&& u)
    {
        RefPtr<PlatformSpeechSynthesisUtterance> utterance = WTFMove(u);
        if (m_shouldCacheUtterance) {
            if (m_firstUtterance.get())
                notifyClient(m_firstUtterance, SpeechSynthesisErrorCode::SynthesisUnavailable);
            m_firstUtterance = utterance;
            return;
        }

        CHECK_TTS_SESSION(utterance);

        if (utterance->text().isEmpty() || utterance->text().length() < 1) {
            notifyClient(utterance, SpeechSynthesisErrorCode::InvalidArgument);
            return;
        }
        if (utterance->text().length() > MAX_ALLOWED_TEXT_LENGTH) {
            notifyClient(utterance, SpeechSynthesisErrorCode::TextTooLong);
            return;
        }

        if (bSpeechSynthOverrideSysTTSConfig) {
            TTS::Configuration config;
            config.volume = utterance->volume() * 100;
            config.rate = (utterance->rate() <= 1.0 ? 50.0 : (utterance->rate() <= 5.0 ? 75.0 : 100.0));

            if ((int)m_TTSVolume != (int)config.volume || (int)m_TTSRate != (int)config.rate) {
                if (m_ttsClient->setTTSConfiguration(config) != TTS::TTS_OK) {
                    notifyClient(utterance, SpeechSynthesisErrorCode::SynthesisFailed);
                    return;
                }
                m_TTSVolume = config.volume;
                m_TTSRate = config.rate;
            }
        }

        static uint32_t s_speechId = 0;
        uint32_t speechId = ++s_speechId;
        m_currentUtteranceSpeechId = 0;

        TTS::SpeechData sdata;
        sdata.text = utterance->text().utf8().data();
        sdata.id = speechId;
        TTS::TTS_Error err = m_ttsClient->speak(m_ttsSessionId, sdata);
        if (err != TTS::TTS_OK) {
            if (err == TTS::TTS_RESOURCE_BUSY || err == TTS::TTS_SESSION_NOT_ACTIVE)
                notifyClient(utterance, SpeechSynthesisErrorCode::AudioBusy);
            else
                notifyClient(utterance, SpeechSynthesisErrorCode::SynthesisFailed);
        } else {
            m_currentUtteranceSpeechId = speechId;
            m_utterancesInProgress.set(m_currentUtteranceSpeechId, utterance);
            m_currentUtterance = utterance;
            if (utterance == m_firstUtterance)
                m_firstUtterance = nullptr;
        }
    }

    void cancel()
    {
        if (m_shouldCacheUtterance && m_firstUtterance.get()) {
            notifyClient(m_firstUtterance, SpeechSynthesisErrorCode::Canceled);
            m_firstUtterance = nullptr;
        }

        CHECK_TTS_SESSION(m_currentUtterance);

        if (m_currentUtterance.get() && m_ttsClient) {
            m_ttsClient->abort(m_ttsSessionId);
            speakingFinished(m_currentUtteranceSpeechId, SpeechSynthesisErrorCode::Interrupted);
        }
    }

    void pause()
    {
        CHECK_TTS_SESSION(m_currentUtterance);

        if (m_currentUtterance.get() && m_ttsClient && m_ttsClient->isSpeaking(m_ttsSessionId)) {
            m_ttsClient->pause(m_ttsSessionId, m_currentUtteranceSpeechId);
        }
    }

    void resume()
    {
        CHECK_TTS_SESSION(m_currentUtterance);

        if (m_currentUtterance.get() && m_ttsClient && !m_ttsClient->isSpeaking(m_ttsSessionId)) {
            m_ttsClient->resume(m_ttsSessionId, m_currentUtteranceSpeechId);
        }
    }

    void notifyClient(RefPtr<PlatformSpeechSynthesisUtterance> utterance, std::optional<SpeechSynthesisErrorCode> error)
    {
        if (!utterance)
            return;

        if (m_currentUtterance == utterance) {
            m_currentUtterance = nullptr;
            m_currentUtteranceSpeechId = 0;
            m_utterancesInProgress.clear();
        }

        if (!error)
            m_platformSynthesizer.client().didFinishSpeaking(*utterance);
        else
            m_platformSynthesizer.client().speakingErrorOccurred(*utterance, error);
    }

    // TTS Client Callbacks

    void onTTSServerConnected()
    {
        RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this)]() {
            if (!weakThis)
                return;
            weakThis->m_ttsConnected = true;
            weakThis->m_shouldCacheUtterance = false;

            if (weakThis->m_ttsClient) {
                if (weakThis->m_ttsSessionId == 0) {
                    weakThis->m_ttsSessionId = weakThis->m_ttsClient->createSession((uint32_t)getpid(), "WPE", weakThis.get());
                    weakThis->m_ttsClient->requestExtendedEvents(weakThis->m_ttsSessionId, 0xFFFF);
                }
                weakThis->voicesDidChange();

                if (weakThis->m_firstUtterance.get())
                    weakThis->speak(weakThis->m_firstUtterance.get());
            }
        });
    }

    void onTTSServerClosed()
    {
        m_ttsConnected = false;
        m_ttsSessionId = 0;

        if (m_currentUtterance.get())
            speakingFinished(m_currentUtteranceSpeechId, SpeechSynthesisErrorCode::Interrupted);
    }

    void onTTSStateChanged(bool) {}

    void onVoiceChanged(std::string)
    {
        RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this)]() {
            if (!weakThis)
                return;
            weakThis->voicesDidChange();
        });
    }

    // TTSSessionCallback functions

    void onTTSSessionCreated(uint32_t, uint32_t) {}
    void onResourceAcquired(uint32_t, uint32_t) {}
    void onResourceReleased(uint32_t, uint32_t) {}

    void onWillSpeak(uint32_t, uint32_t, TTS::SpeechData&) {}

    void onSpeechStart(uint32_t, uint32_t, TTS::SpeechData& speechData)
    {
        RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId = speechData.id]() {
            if (!weakThis)
                return;
            auto index = weakThis->m_utterancesInProgress.find(speechId);
            if (index != weakThis->m_utterancesInProgress.end())
                weakThis->m_platformSynthesizer.client().didStartSpeaking(*index->value);
        });
    }

    void onSpeechPause(uint32_t, uint32_t, uint32_t speechId)
    {
        RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId]() {
            if (!weakThis)
                return;
            auto index = weakThis->m_utterancesInProgress.find(speechId);
            if (index != weakThis->m_utterancesInProgress.end())
                weakThis->m_platformSynthesizer.client().didPauseSpeaking(*index->value);
        });
    }

    void onSpeechResume(uint32_t, uint32_t, uint32_t speechId)
    {
        RunLoop::main().dispatch([weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId]() {
            if (!weakThis)
                return;
            auto index = weakThis->m_utterancesInProgress.find(speechId);
            if (index != weakThis->m_utterancesInProgress.end())
                weakThis->m_platformSynthesizer.client().didResumeSpeaking(*index->value);
        });
    }

    void onSpeechCancelled(uint32_t, uint32_t, uint32_t speechId)
    {
        speakingFinished(speechId, SpeechSynthesisErrorCode::Canceled);
    }

    void onSpeechInterrupted(uint32_t, uint32_t, uint32_t speechId)
    {
        speakingFinished(speechId, SpeechSynthesisErrorCode::Interrupted);
    }

    void onNetworkError(uint32_t, uint32_t, uint32_t speechId)
    {
        speakingFinished(speechId, SpeechSynthesisErrorCode::Network);
    }

    void onPlaybackError(uint32_t, uint32_t, uint32_t speechId)
    {
        speakingFinished(speechId, SpeechSynthesisErrorCode::SynthesisFailed);
    }

    void onSpeechComplete(uint32_t, uint32_t, TTS::SpeechData& speechData)
    {
        speakingFinished(speechData.id, std::nullopt);
    }

private:
    // Internal helper to call when speech is finished.
    // Can be called from both main thread (e.g cancel() func) or TTS callback thread
    void speakingFinished(uint32_t speechId, std::optional<SpeechSynthesisErrorCode> error)
    {
        auto speakingFinishedInternal = [weakThis = m_weakPtrFactory.createWeakPtr(*this), speechId, error]() {
            if (!weakThis)
                return;
            auto index = weakThis->m_utterancesInProgress.find(speechId);
            if (index != weakThis->m_utterancesInProgress.end()) {
                auto utterance = index->value;
                weakThis->m_utterancesInProgress.remove(index);
                weakThis->notifyClient(utterance, error);
            }
        };

        if (RunLoop::isMain())
            speakingFinishedInternal();
        else
            RunLoop::main().dispatch(WTFMove(speakingFinishedInternal));
    }

    void voicesDidChange()
    {
        m_platformSynthesizer.voicesDidChange();
    }

    PlatformSpeechSynthesizer& m_platformSynthesizer;
    WeakPtrFactory<TTSClientPlatformSpeechSynthesizerWrapper> m_weakPtrFactory;

    RefPtr<PlatformSpeechSynthesisUtterance> m_firstUtterance;
    RefPtr<PlatformSpeechSynthesisUtterance> m_currentUtterance;
    HashMap<uint32_t, RefPtr<PlatformSpeechSynthesisUtterance>> m_utterancesInProgress;
    bool m_shouldCacheUtterance;

    TTS::TTSClient* m_ttsClient { nullptr };
    uint32_t m_ttsSessionId;
    bool m_ttsConnected;
    uint32_t m_currentUtteranceSpeechId;

    static double m_TTSVolume;
    static double m_TTSRate;
};

double TTSClientPlatformSpeechSynthesizerWrapper::m_TTSVolume = 0.0;
double TTSClientPlatformSpeechSynthesizerWrapper::m_TTSRate = 0.0;

// PlatformSpeechSynthesizer (Thin Wrapper Delegating to m_platformSpeechWrapper)

WEBCORE_EXPORT Ref<PlatformSpeechSynthesizer> PlatformSpeechSynthesizer::create(PlatformSpeechSynthesizerClient& client)
{
    return adoptRef(*new PlatformSpeechSynthesizer(client));
}

WEBCORE_EXPORT PlatformSpeechSynthesizer::PlatformSpeechSynthesizer(PlatformSpeechSynthesizerClient& client)
    : m_speechSynthesizerClient(client)
    , m_platformSpeechWrapper(std::make_unique<TTSClientPlatformSpeechSynthesizerWrapper>(*this))
{
}

WEBCORE_EXPORT PlatformSpeechSynthesizer::~PlatformSpeechSynthesizer() {}

void PlatformSpeechSynthesizer::speak(RefPtr<PlatformSpeechSynthesisUtterance>&& u)
{
    m_platformSpeechWrapper->speak(WTFMove(u));
}

void PlatformSpeechSynthesizer::pause()
{
    m_platformSpeechWrapper->pause();
}

void PlatformSpeechSynthesizer::resume()
{
    m_platformSpeechWrapper->resume();
}

void PlatformSpeechSynthesizer::cancel()
{
    m_platformSpeechWrapper->cancel();
}

void PlatformSpeechSynthesizer::initializeVoiceList()
{
    m_platformSpeechWrapper->fillVoiceList(m_voiceList);
}

void PlatformSpeechSynthesizer::resetState() {}

} // namespace WebCore

#endif // ENABLE(SPEECH_SYNTHESIS) && USE(TTS_CLIENT)
