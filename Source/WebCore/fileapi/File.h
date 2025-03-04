/*
 * Copyright (C) 2008 Apple Inc. All Rights Reserved.
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

#include "Blob.h"
#include <wtf/Optional.h>
#include <wtf/Ref.h>
#include <wtf/TypeCasts.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class URL;

class File final : public Blob {
public:
    static Ref<File> create(const String& path)
    {
        return adoptRef(*new File(path));
    }

    // Create a File using the 'new File' constructor.
    static Ref<File> create(Vector<BlobPart> blobParts, const String& filename, const String& contentType, int64_t lastModified)
    {
        return adoptRef(*new File(WTFMove(blobParts), filename, contentType, lastModified));
    }

    static Ref<File> deserialize(const String& path, const URL& srcURL, const String& type, const String& name)
    {
        return adoptRef(*new File(deserializationContructor, path, srcURL, type, name));
    }

    // Create a file with a name exposed to the author (via File.name and associated DOM properties) that differs from the one provided in the path.
    static Ref<File> createWithName(const String& path, const String& nameOverride)
    {
        if (nameOverride.isEmpty())
            return adoptRef(*new File(path));
        return adoptRef(*new File(path, nameOverride));
    }

    bool isFile() const override { return true; }

    const String& path() const { return m_path; }
    const String& name() const { return m_name; }
    WEBCORE_EXPORT double lastModified() const;

    static String contentTypeForFile(const String& path);

#if ENABLE(FILE_REPLACEMENT)
    static bool shouldReplaceFile(const String& path);
#endif

private:
    WEBCORE_EXPORT explicit File(const String& path);
    File(const String& path, const String& nameOverride);
    File(Vector<BlobPart>&& blobParts, const String& filename, const String& contentType, int64_t lastModified);

    File(DeserializationContructor, const String& path, const URL& srcURL, const String& type, const String& name);

    static void computeNameAndContentType(const String& path, const String& nameOverride, String& effectiveName, String& effectiveContentType);
#if ENABLE(FILE_REPLACEMENT)
    static void computeNameAndContentTypeForReplacedFile(const String& path, const String& nameOverride, String& effectiveName, String& effectiveContentType);
#endif

    String m_path;
    String m_name;

    Optional<int64_t> m_overrideLastModifiedDate;
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_BEGIN(WebCore::File)
    static bool isType(const WebCore::Blob& blob) { return blob.isFile(); }
SPECIALIZE_TYPE_TRAITS_END()
