// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#define _SCL_SECURE_NO_WARNINGS

#include "TerrainConversion.h"
#include "../../SceneEngine/Terrain.h"
#include "../../SceneEngine/TerrainFormat.h"
#include "../../SceneEngine/TerrainConfig.h"
#include "../../SceneEngine/TerrainConversion.h"
#include "../../Math/Vector.h"
#include "../../ConsoleRig/IProgress.h"
#include "../../ConsoleRig/Log.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/Streams/FileUtils.h"
#include "../../Utility/Streams/PathUtils.h"
#include <vector>
#include <regex>

#include "../../Foreign/LibTiff/tiff.h"
#include "../../Foreign/LibTiff/tiffio.h"

namespace ToolsRig
{
    
    class DEMConfig
    {
    public:
        UInt2 _dims;

        DEMConfig(const char inputHdr[]);
    };

    static void TIFFWarningHandler(const char* module, const char* fmt, va_list args)
    {
        // suppress warnings
        char buffer[1024];
        _vsnprintf_s(buffer, dimof(buffer), _TRUNCATE, fmt, args);
        LogWarning << "Tiff reader warning: " << buffer;
    }

    DEMConfig::DEMConfig(const char inputHdr[])
    {
        _dims = UInt2(0, 0);

        auto ext = XlExtension(inputHdr);
        if (ext && (!XlCompareStringI(ext, "hdr") || !XlCompareStringI(ext, "flt"))) {

            ::Assets::ResChar inputFile[MaxPath];
            XlCopyString(inputFile, inputHdr);
            XlChopExtension(inputFile);
            XlCatString(inputFile, dimof(inputFile), ".hdr");

            size_t fileSize = 0;
            auto block = LoadFileAsMemoryBlock(inputFile, &fileSize);
            std::string configAsString(block.get(), &block[fileSize]);
            std::regex parse("^(\\S+)\\s+(.*)");

            std::vector<int> captureGroups;
            captureGroups.push_back(1);
            captureGroups.push_back(2);
    
            const std::sregex_token_iterator end;
            std::sregex_token_iterator iter(configAsString.begin(), configAsString.end(), parse, captureGroups);
            for (;iter != end;) {
                auto paramName = *iter++;
                auto paramValue = *iter++;

                    //  we ignore many parameters. But we at least need to get ncols & nrows
                    //  These tell us the dimensions of the input data
                if (!XlCompareStringI(paramName.str().c_str(), "ncols")) { _dims[0] = XlAtoI32(paramValue.str().c_str()); }
                if (!XlCompareStringI(paramName.str().c_str(), "nrows")) { _dims[1] = XlAtoI32(paramValue.str().c_str()); }
            }

        } else if (ext && (!XlCompareStringI(ext, "tif") || !XlCompareStringI(ext, "tiff"))) {
            
            auto* tif = TIFFOpen(inputHdr, "r");
            if (tif) {
                TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &_dims[0]);
                TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &_dims[1]);
            }

        }
    }

    class TerrainUberHeader
    {
    public:
        unsigned _magic;
        unsigned _width, _height;
        unsigned _dummy;

        static const unsigned Magic = 0xa3d3e3c3;
    };

    UInt2 ConvertDEMData(
        const ::Assets::ResChar outputDir[], const ::Assets::ResChar input[], 
        unsigned destNodeDims, unsigned destCellTreeDepth,
        ConsoleRig::IProgress* progress)
    {
        auto initStep = progress ? progress->BeginStep("Load source data", 1, false) : nullptr;

        TIFFSetWarningHandler(&TIFFWarningHandler);

        DEMConfig inCfg(input);
        if (!(inCfg._dims[0]*inCfg._dims[1])) {
            ThrowException(
                ::Exceptions::BasicLabel("Bad or missing input terrain config file (%s)", input));
        }

            //  we have to make sure the width and height are multiples of the
            //  dimensions of a cell (in elements). We'll pad out the edges if
            //  they don't match
        const unsigned cellWidthInNodes = 1<<(destCellTreeDepth-1);
        const unsigned clampingDim = destNodeDims * cellWidthInNodes;
        UInt2 finalDims = inCfg._dims;
        if ((finalDims[0] % clampingDim) != 0) { finalDims[0] += clampingDim - (finalDims[0] % clampingDim); }
        if ((finalDims[1] % clampingDim) != 0) { finalDims[1] += clampingDim - (finalDims[1] % clampingDim); }

        CreateDirectoryRecursive(outputDir);

        uint64 resultSize = 
            sizeof(TerrainUberHeader)
            + finalDims[0] * finalDims[1] * sizeof(float)
            ;

        ::Assets::ResChar outputUberFileName[MaxPath]; 
        SceneEngine::TerrainConfig::GetUberSurfaceFilename(
            outputUberFileName, dimof(outputUberFileName),
            outputDir, SceneEngine::CoverageId_Heights);

        MemoryMappedFile outputUberFile(outputUberFileName, resultSize, MemoryMappedFile::Access::Write);
        if (!outputUberFile.IsValid())
            ThrowException(::Exceptions::BasicLabel("Couldn't open output file (%s)", outputUberFile));

        auto& hdr   = *(TerrainUberHeader*)outputUberFile.GetData();
        hdr._magic  = TerrainUberHeader::Magic;
        hdr._width  = finalDims[0];
        hdr._height = finalDims[1];
        hdr._dummy  = 0;

        float* outputArray = (float*)PtrAdd(outputUberFile.GetData(), sizeof(TerrainUberHeader));

        auto ext = XlExtension(input);

        if (ext && (!XlCompareStringI(ext, "hdr") || !XlCompareStringI(ext, "flt"))) {
            MemoryMappedFile inputFileData(input, 0, MemoryMappedFile::Access::Read);
            if (!inputFileData.IsValid())
                ThrowException(::Exceptions::BasicLabel("Couldn't open input file (%s)", input));

            if (initStep) {
                initStep->Advance();
                initStep.reset();
            }

            auto copyRows = std::min(finalDims[1], inCfg._dims[1]);
            const unsigned progressStep = 16;
            auto copyStep = progress ? progress->BeginStep("Create uber surface data", copyRows / progressStep, true) : nullptr;

            auto inputArray = (const float*)inputFileData.GetData();

            unsigned y2=0;
            for (; (y2+progressStep)<=copyRows; y2+=progressStep) {
                for (unsigned y=0; y<progressStep; ++y) {
                    std::copy(
                        &inputArray[(y2+y) * inCfg._dims[0]],
                        &inputArray[(y2+y) * inCfg._dims[0] + std::min(inCfg._dims[0], finalDims[0])],
                        &outputArray[(y2+y) * finalDims[0]]);
                }

                if (copyStep) {
                    if (copyStep->IsCancelled())
                        ThrowException(::Exceptions::BasicLabel("User cancelled"));
                    copyStep->Advance();
                }
            }

                // remainder rows left over after dividing by progressStep
            for (; y2<copyRows; ++y2) {
                std::copy(
                    &inputArray[y2 * inCfg._dims[0]],
                    &inputArray[y2 * inCfg._dims[0] + std::min(inCfg._dims[0], finalDims[0])],
                    &outputArray[y2 * finalDims[0]]);
            }
        } else if (ext && (!XlCompareStringI(ext, "tif") || !XlCompareStringI(ext, "tiff"))) {
                // attempt to read geotiff file
            auto* tif = TIFFOpen(input, "r");
            if (!tif)
                ThrowException(::Exceptions::BasicLabel("Couldn't open input file (%s)", input));

            // auto buf = _TIFFmalloc(TIFFStripSize(tif));
            auto stripCount = TIFFNumberOfStrips(tif);

            auto copyStep = 
                progress 
                ? progress->BeginStep("Create uber surface data", stripCount, true)
                : nullptr;

            uint32 rowsperstrip = 1;
            TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip);

                // assuming that we're going to load in an array of floats here
                // Well, tiff can store other types of elements... But we'll just
                // assume it's want we want
            for (tstrip_t strip = 0; strip < stripCount; strip++) {
                TIFFReadEncodedStrip(tif, strip, &outputArray[strip * rowsperstrip * finalDims[0]], (tsize_t) -1);

                if (copyStep) {
                    if (copyStep->IsCancelled())
                        ThrowException(::Exceptions::BasicLabel("User cancelled"));
                    copyStep->Advance();
                }
            }

            // _TIFFfree(buf);
            TIFFClose(tif);
        }

            // fill in the extra space caused by rounding up
        if (finalDims[0] > inCfg._dims[0]) {
            for (unsigned y=0; y<inCfg._dims[1]; ++y) {
                std::fill(
                    &outputArray[y * finalDims[0] + inCfg._dims[0]],
                    &outputArray[y * finalDims[0] + finalDims[0]],
                    0.f);
            }
        }

        for (unsigned y=inCfg._dims[1]; y < finalDims[1]; ++y) {
            std::fill(
                &outputArray[y * finalDims[0]],
                &outputArray[y * finalDims[0] + finalDims[0]],
                0.f);
        }

        return UInt2(finalDims[0] / clampingDim, finalDims[1] / clampingDim);
    }

    static UInt2 GetUberSurfaceDimensions(const ::Assets::ResChar fn[])
    {
        BasicFile file(fn, "rb", BasicFile::ShareMode::Read|BasicFile::ShareMode::Write);
        TerrainUberHeader hdr;
        if ((file.Read(&hdr, sizeof(hdr), 1) != 1) || (hdr._magic != TerrainUberHeader::Magic))
            ThrowException(::Exceptions::BasicLabel("Error while reading from: (%s)", fn));
        return UInt2(hdr._width, hdr._height);
    }



    void GenerateStarterCells(
        const ::Assets::ResChar outputDir[], const ::Assets::ResChar inputUberSurfaceDirectory[],
        unsigned destNodeDims, unsigned destCellTreeDepth, unsigned overlap, float spacing,
        const std::pair<SceneEngine::TerrainCoverageId, unsigned> layers[], unsigned layerCount,
        ConsoleRig::IProgress* progress)
    {
        using namespace SceneEngine;

        ::Assets::ResChar uberSurfaceHeights[MaxPath]; 
        TerrainConfig::GetUberSurfaceFilename(
            uberSurfaceHeights, dimof(uberSurfaceHeights),
            inputUberSurfaceDirectory, SceneEngine::CoverageId_Heights);
        auto eleCount = GetUberSurfaceDimensions(uberSurfaceHeights);

        auto cellDimsInEles = (1 << (destCellTreeDepth - 1)) * destNodeDims;
        if ((eleCount[0] % cellDimsInEles)!=0 || (eleCount[1] % cellDimsInEles)!=0)
            ThrowException(::Exceptions::BasicLabel("Uber surface size is not divisable by cell size (uber surface size:(%ix%i), cell size:(%i))", 
            eleCount[0], eleCount[1], cellDimsInEles));

        CreateDirectoryRecursive(outputDir);

        TerrainConfig cfg(
            outputDir, eleCount / cellDimsInEles, 
            TerrainConfig::XLE, destNodeDims, destCellTreeDepth, overlap, spacing);

        for (unsigned l=0; l<layerCount; ++l) {
            ::Assets::ResChar uberSurfaceFN[MaxPath]; 
            TerrainConfig::GetUberSurfaceFilename(
                uberSurfaceFN, dimof(uberSurfaceFN),
                inputUberSurfaceDirectory, layers[l].first);

            const unsigned layerRes = 4;
            cfg.AddCoverageLayer(TerrainConfig::CoverageLayer
                {
                    (const utf8*)uberSurfaceFN, layers[l].first,
                    UInt2(layerRes*destNodeDims, layerRes*destNodeDims), 1, layers[l].second
                });
        }

        cfg.Save();
        
        auto fmt = std::make_shared<TerrainFormat>();
        GenerateMissingUberSurfaceFiles(cfg, fmt, inputUberSurfaceDirectory, progress);
        GenerateMissingCellFiles(cfg, fmt, inputUberSurfaceDirectory, progress);
    }
}