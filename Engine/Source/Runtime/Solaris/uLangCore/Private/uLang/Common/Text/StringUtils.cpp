// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Common/Text/StringUtils.h"
#include "uLang/Common/Common.h"
#include "uLang/Common/Text/UTF8StringView.h"
#include "uLang/Common/Text/Unicode.h"
#include <string.h>

namespace uLang
{

bool SplitString(const char* ToSplit, const char* Delim, CUTF8StringView& OutLeft, CUTF8StringView& OutRight)
{
    CUTF8StringView ToSplitStrView(ToSplit);
    CUTF8StringView DelimStrView(Delim);

    UTF8Char* DelimStart = (UTF8Char*)std::strstr(ToSplit, Delim);
    if (DelimStart)
    {
        OutLeft = CUTF8StringView(ToSplitStrView._Begin, DelimStart);
        OutRight = CUTF8StringView(((uint8_t*)DelimStart) + DelimStrView.ByteLen(), ToSplitStrView._End);
    }

    return DelimStart != nullptr;
}

bool SplitString(const char* ToSplit, const UTF8Char Delim, CUTF8StringView& OutLeft, CUTF8StringView& OutRight, bool bReverse)
{
    CUTF8StringView ToSplitStrView(ToSplit);

    UTF8Char* DelimPtr = nullptr;
    if (bReverse)
    {
        DelimPtr = (UTF8Char*)std::strrchr(ToSplit, (char)Delim);
    }
    else
    {
        DelimPtr = (UTF8Char*)std::strchr(ToSplit, (char)Delim);
    }

    if (DelimPtr)
    {
        OutLeft = CUTF8StringView(ToSplitStrView._Begin, DelimPtr);
        OutRight = CUTF8StringView(DelimPtr + 1, ToSplitStrView._End);
    }

    return DelimPtr != nullptr;
}

CUTF8String ToUpper(const CUTF8StringView& Str)
{
    return CUTF8String(Str.ByteLen(), [&Str](UTF8Char* Memory)
        {
            for (const UTF8Char* Char = Str._Begin; Char != Str._End; ++Char, ++Memory)
            {
                *Memory = CUnicode::ToUpper_ASCII(*Char);
            }
        });
}

STextRange IndexRangeToTextRange(const SIdxRange& IndexRange, const CUTF8StringView& SourceText)
{
    SIdxRange InIndexRange(IndexRange);
    if (!IndexRange.IsOrdered())
    {
        InIndexRange = {IndexRange._End, IndexRange._Begin};
    }
    STextRange Result = {0, 0, 0, 0};
    if (SourceText.IsEmpty())
    {
        return Result;
    }
    uint32_t StartRow = 0;
    uint32_t StartCol = 0;
    uint32_t Counter = 0;
    for (auto It = SourceText.begin(); It != SourceText.end() && Counter < InIndexRange._Begin; ++It, ++Counter, ++StartCol)
    {
        // NOTE: (YiLiangSiew) This should naturally handle CRLF as well.
        if (*It == '\n')
        {
            ++StartRow;
            StartCol = 0;
        }
    }
    Counter = InIndexRange._Begin;
    uint32_t EndRow = StartRow;
    uint32_t EndCol = StartCol;
    const CUTF8StringView SubView(SourceText);
    SubView.SubViewEnd(InIndexRange.GetLength());
    for (auto It = SubView.begin(); It != SubView.end() && Counter < InIndexRange._End; ++It, ++Counter, ++EndCol)
    {
        if (*It == '\n')
        {
            ++EndRow;
            EndCol = 0;
        }
    }
    if (IndexRange.IsOrdered())
    {
        Result = {StartRow, StartCol, EndRow, EndCol};
    }
    else
    {
        Result = {EndRow, EndCol, StartRow, StartCol};
    }

    return Result;
}

int32_t CountNumLeadingNewLines(const CUTF8StringView& Text)
{
    if (Text.IsEmpty())
    {
        return 0;
    }
    int32_t NumOfNewLines = 0;
    for (auto ItChar = Text.begin(); ItChar != Text.end(); ++ItChar)
    {
        switch (*ItChar)
        {
            case '\n':
                ++NumOfNewLines;
                ULANG_FALLTHROUGH;
            case '\r':
                ULANG_FALLTHROUGH;
            case '\t':
                ULANG_FALLTHROUGH;
            case ' ':
                continue;
            default:
                return NumOfNewLines;
        }
    }
    return NumOfNewLines;
}

int32_t CountNumTrailingNewLines(const CUTF8StringView& Text)
{
    if (Text.IsEmpty())
    {
        return 0;
    }

    int32_t NumOfNewLines = 0;

    for (const UTF8Char* ItChar = Text._End - 1; ItChar != (Text._Begin - 1); --ItChar)
    {
        switch (*ItChar)
        {
            case '\n':
                ++NumOfNewLines;
                ULANG_FALLTHROUGH;
            case '\r':
                ULANG_FALLTHROUGH;
            case '\t':
                ULANG_FALLTHROUGH;
            case ' ':
                continue;
            default:
                return NumOfNewLines;
        }
    }

    return NumOfNewLines;
}

bool HasTrailingNewLine(const CUTF8StringView& Text)
{
    if (Text.IsEmpty())
    {
        return false;
    }
    for (const UTF8Char* ItChar = Text._End - 1; ItChar != Text._Begin; --ItChar)
    {
        switch (*ItChar)
        {
            case '\n':
                ULANG_FALLTHROUGH;
            case '\r':
                return true;
            case '\t':
                ULANG_FALLTHROUGH;
            case ' ':
                continue;
            default:
                return false;
        }
    }
    return false;
}

int32_t GetCurrentIndentationLevel(const int32_t IndentSize, const CUTF8StringView& Text)
{
    if (Text.IsEmpty())
    {
        return 0;
    }
    int32_t Result = 0;
    int32_t SpaceCount = 0;
    for (int32_t Index = Text.ByteLen() - 1; Index != 0; --Index)
    {
        if (Text[Index] == ' ')
        {
            ++SpaceCount;
            if (SpaceCount == IndentSize)
            {
                ++Result;
                SpaceCount = 0;
            }
        }
        else
        {
            break;
        }
    }
    return Result;
}

uLang::CUTF8String FindLongestCommonPrefix(const uLang::TArray<uLang::CUTF8String> Strings)
{
    const int32_t NumStrings = Strings.Num();
    if (NumStrings == 0)
    {
        return "";
    }
    else if (NumStrings == 1)
    {
        return Strings[0];
    }
    const uLang::CUTF8String FirstString = Strings[0];
    const int32_t FirstStringLen = FirstString.ByteLen();
    for (int32_t CurrentByteIndex = 0; CurrentByteIndex < FirstStringLen; ++CurrentByteIndex)
    {
        const uLang::UTF8Char CurrentByte = FirstString.AsUTF8()[CurrentByteIndex];
        for (int32_t NextStringIndex = 1; NextStringIndex < NumStrings; ++NextStringIndex)
        {
            const uLang::CUTF8String NextString = Strings[NextStringIndex];
            if (NextString.IsEmpty())
            {
                return "";
            }
            const int32_t NextStringLen = NextString.ByteLen();
            if (CurrentByteIndex == NextStringLen || NextString.AsUTF8()[CurrentByteIndex] != CurrentByte)
            {
                return uLang::CUTF8StringView(FirstString.AsUTF8(), FirstString.AsUTF8() + CurrentByteIndex);
            }
        }
    }
    return FirstString;
}

}    // namespace uLang
