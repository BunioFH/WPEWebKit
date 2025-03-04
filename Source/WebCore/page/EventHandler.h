/*
 * Copyright (C) 2006-2016 Apple Inc. All rights reserved.
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

#pragma once

#include "Cursor.h"
#include "DragActions.h"
#include "FocusDirection.h"
#include "HitTestRequest.h"
#include "LayoutPoint.h"
#include "PlatformMouseEvent.h"
#include "PlatformWheelEvent.h"
#include "ScrollTypes.h"
#include "TextEventInputType.h"
#include "TextGranularity.h"
#include "Timer.h"
#include "WheelEventDeltaFilter.h"
#include <memory>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>
#include <wtf/WeakPtr.h>

#if PLATFORM(IOS)
OBJC_CLASS WebEvent;
OBJC_CLASS WAKView;
#ifdef __OBJC__
#include "WAKAppKitStubs.h"
#endif
#endif // PLATFORM(IOS)

#if PLATFORM(COCOA)
OBJC_CLASS NSView;
#endif

namespace WebCore {

class AutoscrollController;
class ContainerNode;
class DataTransfer;
class Document;
class Element;
class Event;
class EventTarget;
class FloatPoint;
class FloatQuad;
class Frame;
class HTMLFrameSetElement;
class HitTestRequest;
class HitTestResult;
class KeyboardEvent;
class MouseEventWithHitTestResults;
class Node;
class OptionalCursor;
class PlatformGestureEvent;
class PlatformKeyboardEvent;
class PlatformTouchEvent;
class PlatformWheelEvent;
class RenderBox;
class RenderElement;
class RenderLayer;
class RenderWidget;
class ScrollableArea;
class Scrollbar;
class TextEvent;
class Touch;
class TouchEvent;
class VisibleSelection;
class WheelEvent;
class Widget;

struct DragState;

#if ENABLE(DRAG_SUPPORT)
extern const int LinkDragHysteresis;
extern const int ImageDragHysteresis;
extern const int TextDragHysteresis;
extern const int GeneralDragHysteresis;
#endif // ENABLE(DRAG_SUPPORT)

#if ENABLE(IOS_GESTURE_EVENTS) || ENABLE(MAC_GESTURE_EVENTS)
extern const float GestureUnknown;
extern const unsigned InvalidTouchIdentifier;
#endif

enum AppendTrailingWhitespace { ShouldAppendTrailingWhitespace, DontAppendTrailingWhitespace };
enum CheckDragHysteresis { ShouldCheckDragHysteresis, DontCheckDragHysteresis };

enum class ImmediateActionStage {
    None,
    PerformedHitTest,
    ActionUpdated,
    ActionCancelledWithoutUpdate,
    ActionCancelledAfterUpdate,
    ActionCompleted
};

class EventHandler {
    WTF_MAKE_NONCOPYABLE(EventHandler);
    WTF_MAKE_FAST_ALLOCATED;
public:
    explicit EventHandler(Frame&);
    ~EventHandler();

    void clear();
    void nodeWillBeRemoved(Node&);

#if ENABLE(DRAG_SUPPORT)
    void updateSelectionForMouseDrag();
#endif

#if ENABLE(PAN_SCROLLING)
    void didPanScrollStart();
    void didPanScrollStop();
    void startPanScrolling(RenderElement*);
#endif

    void stopAutoscrollTimer(bool rendererIsBeingDestroyed = false);
    RenderBox* autoscrollRenderer() const;
    void updateAutoscrollRenderer();
    bool autoscrollInProgress() const;
    bool mouseDownWasInSubframe() const { return m_mouseDownWasInSubframe; }
    bool panScrollInProgress() const;

    WEBCORE_EXPORT void dispatchFakeMouseMoveEventSoon();
    void dispatchFakeMouseMoveEventSoonInQuad(const FloatQuad&);

    WEBCORE_EXPORT HitTestResult hitTestResultAtPoint(const LayoutPoint&,
        HitTestRequest::HitTestRequestType hitType = HitTestRequest::ReadOnly | HitTestRequest::Active | HitTestRequest::DisallowUserAgentShadowContent,
        const LayoutSize& padding = LayoutSize());

    bool mousePressed() const { return m_mousePressed; }
    Node* mousePressNode() const { return m_mousePressNode.get(); }

    WEBCORE_EXPORT void setCapturingMouseEventsElement(PassRefPtr<Element>); // A caller is responsible for resetting capturing element to 0.

#if ENABLE(DRAG_SUPPORT)
    bool updateDragAndDrop(const PlatformMouseEvent&, DataTransfer*);
    void cancelDragAndDrop(const PlatformMouseEvent&, DataTransfer*);
    bool performDragAndDrop(const PlatformMouseEvent&, DataTransfer*);
    void updateDragStateAfterEditDragIfNeeded(Element* rootEditableElement);
#endif

    void scheduleHoverStateUpdate();
#if ENABLE(CURSOR_SUPPORT)
    void scheduleCursorUpdate();
#endif

    void setResizingFrameSet(HTMLFrameSetElement*);

    void resizeLayerDestroyed();

    IntPoint lastKnownMousePosition() const;
    IntPoint lastKnownMouseGlobalPosition() const { return m_lastKnownMouseGlobalPosition; }
    Cursor currentMouseCursor() const { return m_currentMouseCursor; }

    IntPoint effectiveMousePositionForSelectionAutoscroll() const;

    static Frame* subframeForTargetNode(Node*);
    static Frame* subframeForHitTestResult(const MouseEventWithHitTestResults&);

    WEBCORE_EXPORT bool scrollOverflow(ScrollDirection, ScrollGranularity, Node* startingNode = nullptr);
    WEBCORE_EXPORT bool scrollRecursively(ScrollDirection, ScrollGranularity, Node* startingNode = nullptr);
    WEBCORE_EXPORT bool logicalScrollRecursively(ScrollLogicalDirection, ScrollGranularity, Node* startingNode = nullptr);

    bool tabsToLinks(KeyboardEvent*) const;
    bool tabsToAllFormControls(KeyboardEvent*) const;

    WEBCORE_EXPORT bool mouseMoved(const PlatformMouseEvent&);
    WEBCORE_EXPORT bool passMouseMovedEventToScrollbars(const PlatformMouseEvent&);

    void lostMouseCapture();

    WEBCORE_EXPORT bool handleMousePressEvent(const PlatformMouseEvent&);
    bool handleMouseMoveEvent(const PlatformMouseEvent&, HitTestResult* hoveredNode = nullptr, bool onlyUpdateScrollbars = false);
    WEBCORE_EXPORT bool handleMouseReleaseEvent(const PlatformMouseEvent&);
    bool handleMouseForceEvent(const PlatformMouseEvent&);
    WEBCORE_EXPORT bool handleWheelEvent(const PlatformWheelEvent&);
    void defaultWheelEventHandler(Node*, WheelEvent&);
    bool handlePasteGlobalSelection(const PlatformMouseEvent&);

    void platformPrepareForWheelEvents(const PlatformWheelEvent&, const HitTestResult&, RefPtr<Element>& eventTarget, RefPtr<ContainerNode>& scrollableContainer, WeakPtr<ScrollableArea>&, bool& isOverWidget);
    void platformRecordWheelEvent(const PlatformWheelEvent&);
    bool platformCompleteWheelEvent(const PlatformWheelEvent&, ContainerNode* scrollableContainer, const WeakPtr<ScrollableArea>&);
    bool platformCompletePlatformWidgetWheelEvent(const PlatformWheelEvent&, const Widget&, ContainerNode* scrollableContainer);
    void platformNotifyIfEndGesture(const PlatformWheelEvent&, const WeakPtr<ScrollableArea>&);

#if ENABLE(IOS_TOUCH_EVENTS) || ENABLE(IOS_GESTURE_EVENTS)
    typedef Vector<RefPtr<Touch>> TouchArray;
    typedef HashMap<EventTarget*, TouchArray*> EventTargetTouchMap;
#endif

#if ENABLE(IOS_TOUCH_EVENTS) || ENABLE(IOS_GESTURE_EVENTS) || ENABLE(MAC_GESTURE_EVENTS)
    typedef HashSet<RefPtr<EventTarget>> EventTargetSet;
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
    bool dispatchTouchEvent(const PlatformTouchEvent&, const AtomicString&, const EventTargetTouchMap&, float, float);
    bool dispatchSimulatedTouchEvent(IntPoint location);
#endif

#if ENABLE(IOS_GESTURE_EVENTS)
    bool dispatchGestureEvent(const PlatformTouchEvent&, const AtomicString&, const EventTargetSet&, float, float);
#elif ENABLE(MAC_GESTURE_EVENTS)
    bool dispatchGestureEvent(const PlatformGestureEvent&, const AtomicString&, const EventTargetSet&, float, float);
    WEBCORE_EXPORT bool handleGestureEvent(const PlatformGestureEvent&);
#endif

#if PLATFORM(IOS)
    void defaultTouchEventHandler(Node*, TouchEvent*);
#endif

#if ENABLE(CONTEXT_MENUS)
    WEBCORE_EXPORT bool sendContextMenuEvent(const PlatformMouseEvent&);
    bool sendContextMenuEventForKey();
#endif

    void setMouseDownMayStartAutoscroll() { m_mouseDownMayStartAutoscroll = true; }

    bool needsKeyboardEventDisambiguationQuirks() const;

    static OptionSet<PlatformEvent::Modifier> accessKeyModifiers();
    WEBCORE_EXPORT bool handleAccessKey(const PlatformKeyboardEvent&);
    WEBCORE_EXPORT bool keyEvent(const PlatformKeyboardEvent&);
    void defaultKeyboardEventHandler(KeyboardEvent&);

    bool accessibilityPreventsEventPropogation(KeyboardEvent&);
    WEBCORE_EXPORT void handleKeyboardSelectionMovementForAccessibility(KeyboardEvent&);

    bool handleTextInputEvent(const String& text, Event* underlyingEvent = nullptr, TextEventInputType = TextEventInputKeyboard);
    void defaultTextInputEventHandler(TextEvent&);

#if ENABLE(DRAG_SUPPORT)
    WEBCORE_EXPORT bool eventMayStartDrag(const PlatformMouseEvent&) const;
    
    WEBCORE_EXPORT void dragSourceEndedAt(const PlatformMouseEvent&, DragOperation);
#endif

    void focusDocumentView();
    
    WEBCORE_EXPORT void sendScrollEvent();

#if PLATFORM(COCOA) && defined(__OBJC__)
#if !PLATFORM(IOS)
    WEBCORE_EXPORT void mouseDown(NSEvent *, NSEvent *correspondingPressureEvent);
    WEBCORE_EXPORT void mouseDragged(NSEvent *, NSEvent *correspondingPressureEvent);
    WEBCORE_EXPORT void mouseUp(NSEvent *, NSEvent *correspondingPressureEvent);
    WEBCORE_EXPORT void mouseMoved(NSEvent *, NSEvent *correspondingPressureEvent);
    WEBCORE_EXPORT void pressureChange(NSEvent *, NSEvent* correspondingPressureEvent);
    WEBCORE_EXPORT bool keyEvent(NSEvent *);
    WEBCORE_EXPORT bool wheelEvent(NSEvent *);
#else
    WEBCORE_EXPORT void mouseDown(WebEvent *);
    WEBCORE_EXPORT void mouseUp(WebEvent *);
    WEBCORE_EXPORT void mouseMoved(WebEvent *);
    WEBCORE_EXPORT bool keyEvent(WebEvent *);
    WEBCORE_EXPORT bool wheelEvent(WebEvent *);
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
    WEBCORE_EXPORT void touchEvent(WebEvent *);
#endif

#if !PLATFORM(IOS)
    WEBCORE_EXPORT void passMouseMovedEventToScrollbars(NSEvent *, NSEvent* correspondingPressureEvent);

    WEBCORE_EXPORT void sendFakeEventsAfterWidgetTracking(NSEvent *initiatingEvent);
#endif

#if !PLATFORM(IOS)
    void setActivationEventNumber(int num) { m_activationEventNumber = num; }

    WEBCORE_EXPORT static NSEvent *currentNSEvent();
    static NSEvent *correspondingPressureEvent();
#else
    static WebEvent *currentEvent();
#endif // !PLATFORM(IOS)
#endif // PLATFORM(COCOA) && defined(__OBJC__)

#if PLATFORM(IOS)
    void invalidateClick();
#endif

#if ENABLE(TOUCH_EVENTS)
    WEBCORE_EXPORT bool handleTouchEvent(const PlatformTouchEvent&);
#endif

    bool useHandCursor(Node*, bool isOverLink, bool shiftKey);
    void updateCursor();

    bool isHandlingWheelEvent() const { return m_isHandlingWheelEvent; }

    WEBCORE_EXPORT void setImmediateActionStage(ImmediateActionStage stage);
    ImmediateActionStage immediateActionStage() const { return m_immediateActionStage; }

    static Widget* widgetForEventTarget(Element* eventTarget);

private:
#if ENABLE(DRAG_SUPPORT)
    static DragState& dragState();
    static const double TextDragDelay;
    
    PassRefPtr<DataTransfer> createDraggingDataTransfer() const;
#endif // ENABLE(DRAG_SUPPORT)

    bool eventActivatedView(const PlatformMouseEvent&) const;
    bool updateSelectionForMouseDownDispatchingSelectStart(Node*, const VisibleSelection&, TextGranularity);
    void selectClosestWordFromHitTestResult(const HitTestResult&, AppendTrailingWhitespace);
    VisibleSelection selectClosestWordFromHitTestResultBasedOnLookup(const HitTestResult&);
    void selectClosestWordFromMouseEvent(const MouseEventWithHitTestResults&);
    void selectClosestContextualWordFromMouseEvent(const MouseEventWithHitTestResults&);
    void selectClosestContextualWordOrLinkFromMouseEvent(const MouseEventWithHitTestResults&);

    bool handleMouseDoubleClickEvent(const PlatformMouseEvent&);

    WEBCORE_EXPORT bool handleMousePressEvent(const MouseEventWithHitTestResults&);
    bool handleMousePressEventSingleClick(const MouseEventWithHitTestResults&);
    bool handleMousePressEventDoubleClick(const MouseEventWithHitTestResults&);
    bool handleMousePressEventTripleClick(const MouseEventWithHitTestResults&);
#if ENABLE(DRAG_SUPPORT)
    bool handleMouseDraggedEvent(const MouseEventWithHitTestResults&);
#endif
    WEBCORE_EXPORT bool handleMouseReleaseEvent(const MouseEventWithHitTestResults&);

    OptionalCursor selectCursor(const HitTestResult&, bool shiftKey);

    void hoverTimerFired();
#if ENABLE(CURSOR_SUPPORT)
    void cursorUpdateTimerFired();
#endif

    bool logicalScrollOverflow(ScrollLogicalDirection, ScrollGranularity, Node* startingNode = nullptr);
    
    bool shouldTurnVerticalTicksIntoHorizontal(const HitTestResult&, const PlatformWheelEvent&) const;
    
    bool mouseDownMayStartSelect() const { return m_mouseDownMayStartSelect; }

    static bool isKeyboardOptionTab(KeyboardEvent*);
    static bool eventInvertsTabsToLinksClientCallResult(KeyboardEvent*);

#if !ENABLE(IOS_TOUCH_EVENTS)
    void fakeMouseMoveEventTimerFired();
    void cancelFakeMouseMoveEvent();
#endif

    bool isInsideScrollbar(const IntPoint&) const;

#if ENABLE(TOUCH_EVENTS)
    bool dispatchSyntheticTouchEventIfEnabled(const PlatformMouseEvent&);
#endif

#if !PLATFORM(IOS)
    void invalidateClick();
#endif

    Node* nodeUnderMouse() const;
    
    void updateMouseEventTargetNode(Node*, const PlatformMouseEvent&, bool fireMouseOverOut);
    void fireMouseOverOut(bool fireMouseOver = true, bool fireMouseOut = true, bool updateLastNodeUnderMouse = true);
    
    MouseEventWithHitTestResults prepareMouseEvent(const HitTestRequest&, const PlatformMouseEvent&);

    bool dispatchMouseEvent(const AtomicString& eventType, Node* target, bool cancelable, int clickCount, const PlatformMouseEvent&, bool setUnder);
#if ENABLE(DRAG_SUPPORT)
    bool dispatchDragEvent(const AtomicString& eventType, Element& target, const PlatformMouseEvent&, DataTransfer*);

    void freeDataTransfer();

    bool handleDrag(const MouseEventWithHitTestResults&, CheckDragHysteresis);
#endif
    bool handleMouseUp(const MouseEventWithHitTestResults&);
#if ENABLE(DRAG_SUPPORT)
    void clearDragState();

    bool dispatchDragSrcEvent(const AtomicString& eventType, const PlatformMouseEvent&);

    bool dragHysteresisExceeded(const FloatPoint&) const;
    bool dragHysteresisExceeded(const IntPoint&) const;
#endif // ENABLE(DRAG_SUPPORT)
    
    bool mouseMovementExceedsThreshold(const FloatPoint&, int pointsThreshold) const;

    bool passMousePressEventToSubframe(MouseEventWithHitTestResults&, Frame* subframe);
    bool passMouseMoveEventToSubframe(MouseEventWithHitTestResults&, Frame* subframe, HitTestResult* hoveredNode = nullptr);
    bool passMouseReleaseEventToSubframe(MouseEventWithHitTestResults&, Frame* subframe);

    bool passSubframeEventToSubframe(MouseEventWithHitTestResults&, Frame* subframe, HitTestResult* hoveredNode = nullptr);

    bool passMousePressEventToScrollbar(MouseEventWithHitTestResults&, Scrollbar*);

    bool passWidgetMouseDownEventToWidget(const MouseEventWithHitTestResults&);
    bool passWidgetMouseDownEventToWidget(RenderWidget*);

    bool passMouseDownEventToWidget(Widget*);
    bool widgetDidHandleWheelEvent(const PlatformWheelEvent&, Widget&);
    bool completeWidgetWheelEvent(const PlatformWheelEvent&, const WeakPtr<Widget>&, const WeakPtr<ScrollableArea>&, ContainerNode*);

    void defaultSpaceEventHandler(KeyboardEvent&);
    void defaultBackspaceEventHandler(KeyboardEvent&);
    void defaultTabEventHandler(KeyboardEvent&);
    void defaultArrowEventHandler(FocusDirection, KeyboardEvent&);

#if ENABLE(DRAG_SUPPORT)
    DragSourceAction updateDragSourceActionsAllowed() const;
#endif

    // The following are called at the beginning of handleMouseUp and handleDrag.  
    // If they return true it indicates that they have consumed the event.
    bool eventLoopHandleMouseUp(const MouseEventWithHitTestResults&);
#if ENABLE(DRAG_SUPPORT)
    bool eventLoopHandleMouseDragged(const MouseEventWithHitTestResults&);
#endif

#if ENABLE(DRAG_SUPPORT)
    void updateSelectionForMouseDrag(const HitTestResult&);
#endif

    void updateLastScrollbarUnderMouse(Scrollbar*, bool);
    
    void setFrameWasScrolledByUser();

    bool capturesDragging() const { return m_capturesDragging; }

#if PLATFORM(COCOA) && defined(__OBJC__)
    NSView *mouseDownViewIfStillGood();

    PlatformMouseEvent currentPlatformMouseEvent() const;
#endif

#if ENABLE(FULLSCREEN_API)
    bool isKeyEventAllowedInFullScreen(const PlatformKeyboardEvent&) const;
#endif

    void setLastKnownMousePosition(const PlatformMouseEvent&);

#if ENABLE(CURSOR_VISIBILITY)
    void startAutoHideCursorTimer();
    void cancelAutoHideCursorTimer();
    void autoHideCursorTimerFired();
#endif

    void clearOrScheduleClearingLatchedStateIfNeeded(const PlatformWheelEvent&);
    void clearLatchedState();

    Frame& m_frame;

    bool m_mousePressed { false };
    bool m_capturesDragging { false };
    RefPtr<Node> m_mousePressNode;

    bool m_mouseDownMayStartSelect { false };
#if ENABLE(DRAG_SUPPORT)
    bool m_mouseDownMayStartDrag { false };
    bool m_dragMayStartSelectionInstead { false };
#endif
    bool m_mouseDownWasSingleClickInSelection { false };
    enum SelectionInitiationState { HaveNotStartedSelection, PlacedCaret, ExtendedSelection };
    SelectionInitiationState m_selectionInitiationState { HaveNotStartedSelection };

#if ENABLE(DRAG_SUPPORT)
    LayoutPoint m_dragStartPos;
#endif

    bool m_panScrollButtonPressed { false };

    Timer m_hoverTimer;
#if ENABLE(CURSOR_SUPPORT)
    Timer m_cursorUpdateTimer;
#endif

#if PLATFORM(MAC)
    Timer m_pendingMomentumWheelEventsTimer;
#endif
    std::unique_ptr<AutoscrollController> m_autoscrollController;
    bool m_mouseDownMayStartAutoscroll { false };
    bool m_mouseDownWasInSubframe { false };

#if !ENABLE(IOS_TOUCH_EVENTS)
    Timer m_fakeMouseMoveEventTimer;
#endif

    bool m_svgPan { false };

    RenderLayer* m_resizeLayer { nullptr };

    RefPtr<Element> m_capturingMouseEventsElement;
    bool m_eventHandlerWillResetCapturingMouseEventsElement { false };
    
    RefPtr<Element> m_elementUnderMouse;
    RefPtr<Element> m_lastElementUnderMouse;
    RefPtr<Frame> m_lastMouseMoveEventSubframe;
    WeakPtr<Scrollbar> m_lastScrollbarUnderMouse;
    Cursor m_currentMouseCursor;

    int m_clickCount { 0 };
    RefPtr<Node> m_clickNode;

#if ENABLE(IOS_GESTURE_EVENTS)
    float m_gestureInitialDiameter { GestureUnknown };
    float m_gestureInitialRotation { GestureUnknown };
#endif
#if ENABLE(IOS_GESTURE_EVENTS) || ENABLE(MAC_GESTURE_EVENTS)
    float m_gestureLastDiameter { GestureUnknown };
    float m_gestureLastRotation { GestureUnknown };
    EventTargetSet m_gestureTargets;
#endif
#if ENABLE(MAC_GESTURE_EVENTS)
    bool m_hasActiveGesture { false };
#endif

#if ENABLE(IOS_TOUCH_EVENTS)
    unsigned m_firstTouchID { InvalidTouchIdentifier };

    TouchArray m_touches;
    RefPtr<Frame> m_touchEventTargetSubframe;
#endif

#if ENABLE(DRAG_SUPPORT)
    RefPtr<Element> m_dragTarget;
    bool m_shouldOnlyFireDragOverEvent { false };
#endif
    
    RefPtr<HTMLFrameSetElement> m_frameSetBeingResized;

    LayoutSize m_offsetFromResizeCorner; // In the coords of m_resizeLayer.
    
    bool m_mousePositionIsUnknown { true };
    IntPoint m_lastKnownMousePosition;
    IntPoint m_lastKnownMouseGlobalPosition;
    IntPoint m_mouseDownPos; // In our view's coords.
    double m_mouseDownTimestamp { 0 };
    PlatformMouseEvent m_mouseDown;

#if PLATFORM(COCOA)
    NSView *m_mouseDownView { nullptr };
    bool m_sendingEventToSubview { false };
#if !PLATFORM(IOS)
    int m_activationEventNumber { -1 };
#endif
#endif
#if ENABLE(TOUCH_EVENTS) && !ENABLE(IOS_TOUCH_EVENTS)
    typedef HashMap<int, RefPtr<EventTarget>> TouchTargetMap;
    TouchTargetMap m_originatingTouchPointTargets;
    RefPtr<Document> m_originatingTouchPointDocument;
    unsigned m_originatingTouchPointTargetKey { 0 };
    bool m_touchPressed { false };
#endif

    double m_maxMouseMovedDuration { 0 };
    PlatformEvent::Type m_baseEventType { PlatformEvent::NoType };
    bool m_didStartDrag { false };
    bool m_isHandlingWheelEvent { false };

#if ENABLE(CURSOR_VISIBILITY)
    Timer m_autoHideCursorTimer;
#endif

    ImmediateActionStage m_immediateActionStage { ImmediateActionStage::None };
};

} // namespace WebCore
