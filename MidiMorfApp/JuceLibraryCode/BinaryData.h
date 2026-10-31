/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   DSEG7ModernMiniRegular_ttf;
    const int            DSEG7ModernMiniRegular_ttfSize = 21896;

    extern const char*   DSEG14ModernMiniRegular_ttf;
    const int            DSEG14ModernMiniRegular_ttfSize = 27792;

    extern const char*   wood2_jpg;
    const int            wood2_jpgSize = 1010990;

    extern const char*   inBloom_png;
    const int            inBloom_pngSize = 1038506;

    extern const char*   Amen140_wav;
    const int            Amen140_wavSize = 907244;

    extern const char*   Think111_wav;
    const int            Think111_wavSize = 572192;

    extern const char*   FunkyDrummer98_wav;
    const int            FunkyDrummer98_wavSize = 648086;

    extern const char*   FunkyDrummer98b_wav;
    const int            FunkyDrummer98b_wavSize = 648086;

    extern const char*   Impeach94_wav;
    const int            Impeach94_wavSize = 675656;

    extern const char*   SyntheticSub95_wav;
    const int            SyntheticSub95_wavSize = 1337012;

    extern const char*   JungleJazz110_wav;
    const int            JungleJazz110_wavSize = 577394;

    extern const char*   WhoIsShe101_wav;
    const int            WhoIsShe101_wavSize = 628838;

    extern const char*   Icon_png;
    const int            Icon_pngSize = 2135024;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 13;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
