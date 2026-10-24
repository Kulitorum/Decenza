#pragma once

class LanguageOperationPolicy final
{
public:
    static bool batchTranslateUploadAllowed(bool batchActive, bool autoTranslating,
                                            bool uploading)
    {
        return !batchActive && !autoTranslating && !uploading;
    }
};
