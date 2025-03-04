/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2015-2016 Apple Inc. All rights reserved.
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

#include "config.h"
#include "FormSubmission.h"

#include "ContentSecurityPolicy.h"
#include "DOMFormData.h"
#include "Document.h"
#include "Event.h"
#include "FormData.h"
#include "FormDataBuilder.h"
#include "FormState.h"
#include "Frame.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "HTMLFormControlElement.h"
#include "HTMLFormElement.h"
#include "HTMLInputElement.h"
#include "HTMLNames.h"
#include "HTMLParserIdioms.h"
#include "TextEncoding.h"
#include <wtf/CurrentTime.h>

namespace WebCore {

using namespace HTMLNames;

static int64_t generateFormDataIdentifier()
{
    // Initialize to the current time to reduce the likelihood of generating
    // identifiers that overlap with those from past/future browser sessions.
    static int64_t nextIdentifier = static_cast<int64_t>(currentTime() * 1000000.0);
    return ++nextIdentifier;
}

static void appendMailtoPostFormDataToURL(URL& url, const FormData& data, const String& encodingType)
{
    String body = data.flattenToString();

    if (equalLettersIgnoringASCIICase(encodingType, "text/plain")) {
        // Convention seems to be to decode, and s/&/\r\n/. Also, spaces are encoded as %20.
        body = decodeURLEscapeSequences(body.replaceWithLiteral('&', "\r\n").replace('+', ' ') + "\r\n");
    }

    Vector<char> bodyData;
    bodyData.append("body=", 5);
    FormDataBuilder::encodeStringAsFormData(bodyData, body.utf8());
    body = String(bodyData.data(), bodyData.size()).replaceWithLiteral('+', "%20");

    String query = url.query();
    if (query.isEmpty())
        url.setQuery(body);
    else
        url.setQuery(query + '&' + body);
}

void FormSubmission::Attributes::parseAction(const String& action)
{
    // FIXME: Can we parse into a URL?
    m_action = stripLeadingAndTrailingHTMLSpaces(action);
}

String FormSubmission::Attributes::parseEncodingType(const String& type)
{
    if (equalLettersIgnoringASCIICase(type, "multipart/form-data"))
        return ASCIILiteral("multipart/form-data");
    if (equalLettersIgnoringASCIICase(type, "text/plain"))
        return ASCIILiteral("text/plain");
    return ASCIILiteral("application/x-www-form-urlencoded");
}

void FormSubmission::Attributes::updateEncodingType(const String& type)
{
    m_encodingType = parseEncodingType(type);
    m_isMultiPartForm = (m_encodingType == "multipart/form-data");
}

FormSubmission::Method FormSubmission::Attributes::parseMethodType(const String& type)
{
    return equalLettersIgnoringASCIICase(type, "post") ? FormSubmission::PostMethod : FormSubmission::GetMethod;
}

void FormSubmission::Attributes::updateMethodType(const String& type)
{
    m_method = parseMethodType(type);
}

void FormSubmission::Attributes::copyFrom(const Attributes& other)
{
    m_method = other.m_method;
    m_isMultiPartForm = other.m_isMultiPartForm;

    m_action = other.m_action;
    m_target = other.m_target;
    m_encodingType = other.m_encodingType;
    m_acceptCharset = other.m_acceptCharset;
}

inline FormSubmission::FormSubmission(Method method, const URL& action, const String& target, const String& contentType, PassRefPtr<FormState> state, PassRefPtr<FormData> data, const String& boundary, LockHistory lockHistory, PassRefPtr<Event> event)
    : m_method(method)
    , m_action(action)
    , m_target(target)
    , m_contentType(contentType)
    , m_formState(state)
    , m_formData(data)
    , m_boundary(boundary)
    , m_lockHistory(lockHistory)
    , m_event(event)
{
}

static TextEncoding encodingFromAcceptCharset(const String& acceptCharset, Document& document)
{
    String normalizedAcceptCharset = acceptCharset;
    normalizedAcceptCharset.replace(',', ' ');

    Vector<String> charsets;
    normalizedAcceptCharset.split(' ', charsets);

    for (auto& charset : charsets) {
        TextEncoding encoding(charset);
        if (encoding.isValid())
            return encoding;
    }

    return document.textEncoding();
}

Ref<FormSubmission> FormSubmission::create(HTMLFormElement* form, const Attributes& attributes, PassRefPtr<Event> event, LockHistory lockHistory, FormSubmissionTrigger trigger)
{
    ASSERT(form);

    HTMLFormControlElement* submitButton = nullptr;
    if (event && event->target()) {
        for (Node* node = event->target()->toNode(); node; node = node->parentNode()) {
            if (is<HTMLFormControlElement>(*node)) {
                submitButton = downcast<HTMLFormControlElement>(node);
                break;
            }
        }
    }

    FormSubmission::Attributes copiedAttributes;
    copiedAttributes.copyFrom(attributes);
    if (submitButton) {
        AtomicString attributeValue;
        if (!(attributeValue = submitButton->attributeWithoutSynchronization(formactionAttr)).isNull())
            copiedAttributes.parseAction(attributeValue);
        if (!(attributeValue = submitButton->attributeWithoutSynchronization(formenctypeAttr)).isNull())
            copiedAttributes.updateEncodingType(attributeValue);
        if (!(attributeValue = submitButton->attributeWithoutSynchronization(formmethodAttr)).isNull())
            copiedAttributes.updateMethodType(attributeValue);
        if (!(attributeValue = submitButton->attributeWithoutSynchronization(formtargetAttr)).isNull())
            copiedAttributes.setTarget(attributeValue);
    }
    
    Document& document = form->document();
    URL actionURL = document.completeURL(copiedAttributes.action().isEmpty() ? document.url().string() : copiedAttributes.action());
    bool isMailtoForm = actionURL.protocolIs("mailto");
    bool isMultiPartForm = false;
    String encodingType = copiedAttributes.encodingType();

    document.contentSecurityPolicy()->upgradeInsecureRequestIfNeeded(actionURL, ContentSecurityPolicy::InsecureRequestType::FormSubmission);

    if (copiedAttributes.method() == PostMethod) {
        isMultiPartForm = copiedAttributes.isMultiPartForm();
        if (isMultiPartForm && isMailtoForm) {
            encodingType = "application/x-www-form-urlencoded";
            isMultiPartForm = false;
        }
    }

    TextEncoding dataEncoding = isMailtoForm ? UTF8Encoding() : encodingFromAcceptCharset(copiedAttributes.acceptCharset(), document);
    RefPtr<DOMFormData> domFormData = DOMFormData::create(dataEncoding.encodingForFormSubmission());
    Vector<std::pair<String, String>> formValues;

    bool containsPasswordData = false;
    for (auto& control : form->associatedElements()) {
        HTMLElement& element = control->asHTMLElement();
        if (!element.isDisabledFormControl())
            control->appendFormData(*domFormData, isMultiPartForm);
        if (is<HTMLInputElement>(element)) {
            HTMLInputElement& input = downcast<HTMLInputElement>(element);
            if (input.isTextField()) {
                formValues.append(std::pair<String, String>(input.name().string(), input.value()));
                input.addSearchResult();
            }
            if (input.isPasswordField() && !input.value().isEmpty())
                containsPasswordData = true;
        }
    }

    RefPtr<FormData> formData;
    String boundary;

    if (isMultiPartForm) {
        formData = FormData::createMultiPart(*(static_cast<FormDataList*>(domFormData.get())), domFormData->encoding(), &document);
        boundary = formData->boundary().data();
    } else {
        formData = FormData::create(*(static_cast<FormDataList*>(domFormData.get())), domFormData->encoding(), attributes.method() == GetMethod ? FormData::FormURLEncoded : FormData::parseEncodingType(encodingType));
        if (copiedAttributes.method() == PostMethod && isMailtoForm) {
            // Convert the form data into a string that we put into the URL.
            appendMailtoPostFormDataToURL(actionURL, *formData, encodingType);
            formData = FormData::create();
        }
    }

    formData->setIdentifier(generateFormDataIdentifier());
    formData->setContainsPasswordData(containsPasswordData);
    String targetOrBaseTarget = copiedAttributes.target().isEmpty() ? document.baseTarget() : copiedAttributes.target();
    auto formState = FormState::create(form, formValues, &document, trigger);
    return adoptRef(*new FormSubmission(copiedAttributes.method(), actionURL, targetOrBaseTarget, encodingType, WTFMove(formState), WTFMove(formData), boundary, lockHistory, event));
}

URL FormSubmission::requestURL() const
{
    if (m_method == FormSubmission::PostMethod)
        return m_action;

    URL requestURL(m_action);
    requestURL.setQuery(m_formData->flattenToString());    
    return requestURL;
}

void FormSubmission::populateFrameLoadRequest(FrameLoadRequest& frameRequest)
{
    if (!m_target.isEmpty())
        frameRequest.setFrameName(m_target);

    if (!m_referrer.isEmpty())
        frameRequest.resourceRequest().setHTTPReferrer(m_referrer);

    if (m_method == FormSubmission::PostMethod) {
        frameRequest.resourceRequest().setHTTPMethod("POST");
        frameRequest.resourceRequest().setHTTPBody(m_formData.copyRef());

        // construct some user headers if necessary
        if (m_boundary.isEmpty())
            frameRequest.resourceRequest().setHTTPContentType(m_contentType);
        else
            frameRequest.resourceRequest().setHTTPContentType(m_contentType + "; boundary=" + m_boundary);
    }

    frameRequest.resourceRequest().setURL(requestURL());
    FrameLoader::addHTTPOriginIfNeeded(frameRequest.resourceRequest(), m_origin);
    FrameLoader::addHTTPUpgradeInsecureRequestsIfNeeded(frameRequest.resourceRequest());
}

}
