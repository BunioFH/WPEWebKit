/*
 * Copyright (C) 2015, 2016 Apple Inc. All rights reserved.
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

#include "config.h"
#include "JSCustomElementRegistry.h"

#include "CustomElementRegistry.h"
#include "Document.h"
#include "HTMLNames.h"
#include "JSCustomElementInterface.h"
#include "JSDOMBinding.h"
#include "JSDOMConvert.h"
#include "JSDOMPromise.h"

using namespace JSC;

namespace WebCore {

static JSObject* getCustomElementCallback(ExecState& state, JSObject& prototype, const Identifier& id)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    JSValue callback = prototype.get(&state, id);
    RETURN_IF_EXCEPTION(scope, nullptr);
    if (callback.isUndefined())
        return nullptr;
    if (!callback.isFunction()) {
        throwTypeError(&state, scope, ASCIILiteral("A custom element callback must be a function"));
        return nullptr;
    }
    return callback.getObject();
}

static bool validateCustomElementNameAndThrowIfNeeded(ExecState& state, const AtomicString& name)
{
    auto scope = DECLARE_THROW_SCOPE(state.vm());

    switch (Document::validateCustomElementName(name)) {
    case CustomElementNameValidationStatus::Valid:
        return true;
    case CustomElementNameValidationStatus::ConflictsWithBuiltinNames:
        throwSyntaxError(&state, scope, ASCIILiteral("Custom element name cannot be same as one of the builtin elements"));
        return false;
    case CustomElementNameValidationStatus::NoHyphen:
        throwSyntaxError(&state, scope, ASCIILiteral("Custom element name must contain a hyphen"));
        return false;
    case CustomElementNameValidationStatus::ContainsUpperCase:
        throwSyntaxError(&state, scope, ASCIILiteral("Custom element name cannot contain an upper case letter"));
        return false;
    }
    ASSERT_NOT_REACHED();
    return false;
}

// https://html.spec.whatwg.org/#dom-customelementregistry-define
JSValue JSCustomElementRegistry::define(ExecState& state)
{
    VM& vm = state.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (UNLIKELY(state.argumentCount() < 2))
        return throwException(&state, scope, createNotEnoughArgumentsError(&state));

    AtomicString localName(state.uncheckedArgument(0).toString(&state)->toAtomicString(&state));
    RETURN_IF_EXCEPTION(scope, JSValue());

    JSValue constructorValue = state.uncheckedArgument(1);
    if (!constructorValue.isConstructor())
        return throwTypeError(&state, scope, ASCIILiteral("The second argument must be a constructor"));
    JSObject* constructor = constructorValue.getObject();

    if (!validateCustomElementNameAndThrowIfNeeded(state, localName))
        return jsUndefined();

    CustomElementRegistry& registry = wrapped();

    if (registry.elementDefinitionIsRunning()) {
        throwNotSupportedError(state, scope, ASCIILiteral("Cannot define a custom element while defining another custom element"));
        return jsUndefined();
    }
    TemporaryChange<bool> change(registry.elementDefinitionIsRunning(), true);

    if (registry.findInterface(localName)) {
        throwNotSupportedError(state, scope, ASCIILiteral("Cannot define multiple custom elements with the same tag name"));
        return jsUndefined();
    }

    if (registry.containsConstructor(constructor)) {
        throwNotSupportedError(state, scope, ASCIILiteral("Cannot define multiple custom elements with the same class"));
        return jsUndefined();
    }

    JSValue prototypeValue = constructor->get(&state, vm.propertyNames->prototype);
    RETURN_IF_EXCEPTION(scope, JSValue());
    if (!prototypeValue.isObject())
        return throwTypeError(&state, scope, ASCIILiteral("Custom element constructor's prototype must be an object"));
    JSObject& prototypeObject = *asObject(prototypeValue);

    QualifiedName name(nullAtom, localName, HTMLNames::xhtmlNamespaceURI);
    auto elementInterface = JSCustomElementInterface::create(name, constructor, globalObject());

    if (auto* connectedCallback = getCustomElementCallback(state, prototypeObject, Identifier::fromString(&vm, "connectedCallback")))
        elementInterface->setConnectedCallback(connectedCallback);
    RETURN_IF_EXCEPTION(scope, JSValue());

    if (auto* disconnectedCallback = getCustomElementCallback(state, prototypeObject, Identifier::fromString(&vm, "disconnectedCallback")))
        elementInterface->setDisconnectedCallback(disconnectedCallback);
    RETURN_IF_EXCEPTION(scope, JSValue());

    if (auto* adoptedCallback = getCustomElementCallback(state, prototypeObject, Identifier::fromString(&vm, "adoptedCallback")))
        elementInterface->setAdoptedCallback(adoptedCallback);
    RETURN_IF_EXCEPTION(scope, JSValue());

    auto* attributeChangedCallback = getCustomElementCallback(state, prototypeObject, Identifier::fromString(&vm, "attributeChangedCallback"));
    RETURN_IF_EXCEPTION(scope, JSValue());
    if (attributeChangedCallback) {
        auto observedAttributesValue = constructor->get(&state, Identifier::fromString(&state, "observedAttributes"));
        RETURN_IF_EXCEPTION(scope, JSValue());
        if (!observedAttributesValue.isUndefined()) {
            auto observedAttributes = convert<IDLSequence<IDLDOMString>>(state, observedAttributesValue);
            RETURN_IF_EXCEPTION(scope, JSValue());
            elementInterface->setAttributeChangedCallback(attributeChangedCallback, observedAttributes);
        }
    }

    PrivateName uniquePrivateName;
    globalObject()->putDirect(vm, uniquePrivateName, constructor);

    registry.addElementDefinition(WTFMove(elementInterface));

    return jsUndefined();
}

// https://html.spec.whatwg.org/#dom-customelementregistry-whendefined
static JSValue whenDefinedPromise(ExecState& state, JSDOMGlobalObject& globalObject, CustomElementRegistry& registry, JSPromiseDeferred& promiseDeferred)
{
    auto scope = DECLARE_THROW_SCOPE(state.vm());

    if (UNLIKELY(state.argumentCount() < 1))
        return throwException(&state, scope, createNotEnoughArgumentsError(&state));

    AtomicString localName(state.uncheckedArgument(0).toString(&state)->toAtomicString(&state));
    RETURN_IF_EXCEPTION(scope, JSValue());

    if (!validateCustomElementNameAndThrowIfNeeded(state, localName)) {
        ASSERT(scope.exception());
        return jsUndefined();
    }

    if (registry.findInterface(localName)) {
        DeferredPromise::create(globalObject, promiseDeferred)->resolve(nullptr);
        return promiseDeferred.promise();
    }

    auto result = registry.promiseMap().ensure(localName, [&] {
        return DeferredPromise::create(globalObject, promiseDeferred);
    });

    return result.iterator->value->promise();
}

JSValue JSCustomElementRegistry::whenDefined(ExecState& state)
{
    auto scope = DECLARE_CATCH_SCOPE(state.vm());

    ASSERT(globalObject());
    auto promiseDeferred = JSPromiseDeferred::create(&state, globalObject());
    ASSERT(promiseDeferred);
    JSValue promise = whenDefinedPromise(state, *globalObject(), wrapped(), *promiseDeferred);

    if (UNLIKELY(scope.exception())) {
        rejectPromiseWithExceptionIfAny(state, *globalObject(), *promiseDeferred);
        ASSERT(!scope.exception());
        return promiseDeferred->promise();
    }

    return promise;
}

}
