/*
 * Copyright (C) 2006, 2007, 2008, 2009 Apple Inc. All rights reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "HTMLDivElement.h"
#include "HTMLNames.h"
#include "RenderBlockFlow.h"
#include <wtf/Forward.h>

namespace WebCore {

class HTMLInputElement;
class FloatPoint;
class TouchEvent;

class SliderThumbElement final : public HTMLDivElement {
public:
    static Ref<SliderThumbElement> create(Document&);

    void setPositionFromValue();
    void dragFrom(const LayoutPoint&);
    HTMLInputElement* hostInput() const;
    void setPositionFromPoint(const LayoutPoint&);

#if ENABLE(IOS_TOUCH_EVENTS)
    void handleTouchEvent(TouchEvent&);

    void disabledAttributeChanged();
#endif

private:
    SliderThumbElement(Document&);

    RenderPtr<RenderElement> createElementRenderer(RenderStyle&&, const RenderTreePosition&) override;

    Ref<Element> cloneElementWithoutAttributesAndChildren(Document&) override;
    bool isDisabledFormControl() const override;
    bool matchesReadWritePseudoClass() const override;
    Element* focusDelegate() override;
#if !PLATFORM(IOS)
    void defaultEventHandler(Event&) override;
    bool willRespondToMouseMoveEvents() override;
    bool willRespondToMouseClickEvents() override;
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
    void didAttachRenderers() override;
#endif
    void willDetachRenderers() override;

    Optional<ElementStyle> resolveCustomStyle(const RenderStyle&, const RenderStyle*) override;
    const AtomicString& shadowPseudoId() const override;

    void startDragging();
    void stopDragging();

#if ENABLE(IOS_TOUCH_EVENTS)
    unsigned exclusiveTouchIdentifier() const;
    void setExclusiveTouchIdentifier(unsigned);
    void clearExclusiveTouchIdentifier();

    void handleTouchStart(TouchEvent&);
    void handleTouchMove(TouchEvent&);
    void handleTouchEndAndCancel(TouchEvent&);

    bool shouldAcceptTouchEvents();
    void registerForTouchEvents();
    void unregisterForTouchEvents();
#endif

    AtomicString m_shadowPseudoId;
    bool m_inDragMode;

#if ENABLE(IOS_TOUCH_EVENTS)
    // FIXME: Currently it is safe to use 0, but this may need to change
    // if touch identifiers change in the future and can be 0.
    static const unsigned NoIdentifier = 0;
    unsigned m_exclusiveTouchIdentifier;
    bool m_isRegisteredAsTouchEventListener;
#endif
};

inline Ref<SliderThumbElement> SliderThumbElement::create(Document& document)
{
    return adoptRef(*new SliderThumbElement(document));
}

// --------------------------------

class RenderSliderThumb final : public RenderBlockFlow {
public:
    RenderSliderThumb(SliderThumbElement&, RenderStyle&&);
    void updateAppearance(const RenderStyle* parentStyle);

private:
    bool isSliderThumb() const override;
};

// --------------------------------

class SliderContainerElement final : public HTMLDivElement {
public:
    static Ref<SliderContainerElement> create(Document&);

private:
    SliderContainerElement(Document&);
    RenderPtr<RenderElement> createElementRenderer(RenderStyle&&, const RenderTreePosition&) override;
    Optional<ElementStyle> resolveCustomStyle(const RenderStyle&, const RenderStyle*) override;
    const AtomicString& shadowPseudoId() const override;

    AtomicString m_shadowPseudoId;
};

} // namespace WebCore
