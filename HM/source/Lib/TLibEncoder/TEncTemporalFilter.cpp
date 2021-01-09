/* The copyright in this software is being made available under the BSD
* License, included below. This software may be subject to other third party
* and contributor rights, including patent rights, and no such rights are
* granted under this license.
*
* Copyright (c) 2010-2020, ITU/ISO/IEC
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
*  * Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
*    be used to endorse or promote products derived from this software without
*    specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
*/

/** \file     TEncTemporalFilter.cpp
\brief    TEncTemporalFilter class
*/
#include "TEncTemporalFilter.h"
#include <math.h>


// ====================================================================================================================
// Constructor / destructor / initialization / destroy
// ====================================================================================================================

const Int TEncTemporalFilter::s_range = 2;
const Double TEncTemporalFilter::s_chromaFactor = 0.55;
const Double TEncTemporalFilter::s_sigmaMultiplier = 9.0;
const Double TEncTemporalFilter::s_sigmaZeroPoint = 10.0;
const Int TEncTemporalFilter::s_motionVectorFactor = 16;
const Int TEncTemporalFilter::s_padding = 128;
const Int TEncTemporalFilter::s_interpolationFilter[16][8] =
{
    {   0,   0,   0,  64,   0,   0,   0,   0 },   //0
    {   0,   1,  -3,  64,   4,  -2,   0,   0 },   //1 -->-->
    {   0,   1,  -6,  62,   9,  -3,   1,   0 },   //2 -->
    {   0,   2,  -8,  60,  14,  -5,   1,   0 },   //3 -->-->
    {   0,   2,  -9,  57,  19,  -7,   2,   0 },   //4
    {   0,   3, -10,  53,  24,  -8,   2,   0 },   //5 -->-->
    {   0,   3, -11,  50,  29,  -9,   2,   0 },   //6 -->
    {   0,   3, -11,  44,  35, -10,   3,   0 },   //7 -->-->
    {   0,   1,  -7,  38,  38,  -7,   1,   0 },   //8
    {   0,   3, -10,  35,  44, -11,   3,   0 },   //9 -->-->
    {   0,   2,  -9,  29,  50, -11,   3,   0 },   //10-->
    {   0,   2,  -8,  24,  53, -10,   3,   0 },   //11-->-->
    {   0,   2,  -7,  19,  57,  -9,   2,   0 },   //12
    {   0,   1,  -5,  14,  60,  -8,   2,   0 },   //13-->-->
    {   0,   1,  -3,   9,  62,  -6,   1,   0 },   //14-->
    {   0,   0,  -2,   4,  64,  -3,   1,   0 }    //15-->-->
};

const Double TEncTemporalFilter::s_refStrengths[3][2] =
{ // abs(POC offset)
  //  1,    2
  {0.85, 0.60},  // s_range * 2
  {1.20, 1.00},  // s_range
  {0.30, 0.30}   // otherwise
};

TEncTemporalFilter::TEncTemporalFilter() :
  m_FrameSkip(0),
  m_chromaFormatIDC(NUM_CHROMA_FORMAT),
  m_sourceWidth(0),
  m_sourceHeight(0),
  m_QP(0),
  m_GOPSize(0),
  m_framesToBeEncoded(0),
  m_bClipInputVideoToRec709Range(false),
  m_inputColourSpaceConvert(NUMBER_INPUT_COLOUR_SPACE_CONVERSIONS)
{}

void TEncTemporalFilter::init(const Int frameSkip,
                              const Int inputBitDepth[MAX_NUM_CHANNEL_TYPE],
                              const Int MSBExtendedBitDepth[MAX_NUM_CHANNEL_TYPE],
                              const Int internalBitDepth[MAX_NUM_CHANNEL_TYPE],
                              const Int width,
                              const Int height,
                              const Int *pad,
                              const Int frames,
                              const Bool Rec709,
                              const std::string &filename,
                              const ChromaFormat inputChromaFormatIDC,
                              const InputColourSpaceConversion colorSpaceConv,
                              const Int QP,
                              const Int GOPSize,
                              const std::map<Int, Double> &temporalFilterStrengths,
                              const Bool gopBasedTemporalFilterFutureReference)
{
  m_FrameSkip = frameSkip;
  for (Int i = 0; i < MAX_NUM_CHANNEL_TYPE; i++)
  {
    m_inputBitDepth[i] = inputBitDepth[i];
    m_MSBExtendedBitDepth[i] = MSBExtendedBitDepth[i];
    m_internalBitDepth[i] = internalBitDepth[i];
  }

  m_sourceWidth = width;
  m_sourceHeight = height;
  for (Int i = 0; i < 2; i++)
  {
    m_aiPad[i] = pad[i];
  }
  m_framesToBeEncoded = frames; // NOT USED.
  m_bClipInputVideoToRec709Range = Rec709;
  m_inputFileName = filename;
  m_chromaFormatIDC = inputChromaFormatIDC;
  m_inputColourSpaceConvert = colorSpaceConv;
  m_QP = QP;
  m_GOPSize = GOPSize; // NOT USED.
  m_temporalFilterStrengths = temporalFilterStrengths;
  m_gopBasedTemporalFilterFutureReference = gopBasedTemporalFilterFutureReference;
}

// ====================================================================================================================
// Public member functions
// ====================================================================================================================

Bool TEncTemporalFilter::filter(TComPicYuv *orgPic, Int receivedPoc)
{
  Bool isFilterThisFrame = false;
  if (m_QP >= 17)  // disable filter for QP < 17
  {
    for (map<Int, Double>::iterator it = m_temporalFilterStrengths.begin(); it != m_temporalFilterStrengths.end(); ++it)
    {
      Int filteredFrame = it->first;
      if (receivedPoc % filteredFrame == 0)
      {
        isFilterThisFrame = true;
        break;
      }
    }
  }

  if (isFilterThisFrame)
  {
    Int offset = m_FrameSkip;
    TVideoIOYuv yuvFrames;
    yuvFrames.open(m_inputFileName, false, m_inputBitDepth, m_MSBExtendedBitDepth, m_internalBitDepth);
    yuvFrames.skipFrames(std::max(offset + receivedPoc - s_range, 0), m_sourceWidth - m_aiPad[0], m_sourceHeight - m_aiPad[1], m_chromaFormatIDC);


    std::deque<TemporalFilterSourcePicInfo> srcFrameInfo;

    Int firstFrame = receivedPoc + offset - s_range;
    Int lastFrame = receivedPoc + offset + s_range;
    if (!m_gopBasedTemporalFilterFutureReference)
    {
      lastFrame = receivedPoc + offset - 1;
    }
    Int origOffset = -s_range;

    // subsample original picture so it only needs to be done once
    TComPicYuv origPadded;

    origPadded.createWithoutCUInfo(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, true, s_padding, s_padding);
    orgPic->copyToPic(&origPadded);
    origPadded.extendPicBorder();

    TComPicYuv origSubsampled2;
    TComPicYuv origSubsampled4;

    subsampleLuma(origPadded, origSubsampled2);
    subsampleLuma(origSubsampled2, origSubsampled4);

    // determine motion vectors
    for (Int poc = firstFrame; poc <= lastFrame; poc++)
    {
      if (poc < 0)
      {
        origOffset++;
        continue; // frame not available
      }
      else if (poc == offset + receivedPoc)
      { // hop over frame that will be filtered
        yuvFrames.skipFrames(1, m_sourceWidth - m_aiPad[0], m_sourceHeight - m_aiPad[1], m_chromaFormatIDC);
        origOffset++;
        continue;
      }
      srcFrameInfo.push_back(TemporalFilterSourcePicInfo());
      TemporalFilterSourcePicInfo &srcPic=srcFrameInfo.back();

      TComPicYuv     dummyPicBufferTO; // Only used temporary in yuvFrames.read
      srcPic.picBuffer.createWithoutCUInfo(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, true, s_padding, s_padding);
      dummyPicBufferTO.createWithoutCUInfo(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, true, s_padding, s_padding);
      if (!yuvFrames.read(&srcPic.picBuffer, &dummyPicBufferTO, m_inputColourSpaceConvert, m_aiPad, m_chromaFormatIDC, m_bClipInputVideoToRec709Range))
      {
        return false; // eof or read fail
      }
      srcPic.picBuffer.extendPicBorder();
      srcPic.mvs.allocate(m_sourceWidth / 4, m_sourceHeight / 4);

      motionEstimation(srcPic.mvs, origPadded, srcPic.picBuffer, origSubsampled2, origSubsampled4);
      srcPic.origOffset = origOffset;
      origOffset++;
    }

    // filter
    TComPicYuv newOrgPic;
    newOrgPic.createWithoutCUInfo(m_sourceWidth, m_sourceHeight, m_chromaFormatIDC, true, s_padding, s_padding);
    Double overallStrength = -1.0;
    for (map<Int, Double>::iterator it = m_temporalFilterStrengths.begin(); it != m_temporalFilterStrengths.end(); ++it)
    {
      Int frame = it->first;
      Double strength = it->second;
      if (receivedPoc % frame == 0)
      {
        overallStrength = strength;
      }
    }

    bilateralFilter(origPadded, srcFrameInfo, newOrgPic, overallStrength);

    // move filtered to orgPic
    newOrgPic.copyToPic(orgPic);

    yuvFrames.close();
    return true;
  }
  return false;
}

// ====================================================================================================================
// Private member functions
// ====================================================================================================================

Void TEncTemporalFilter::subsampleLuma(const TComPicYuv &input, TComPicYuv &output, const Int factor) const
{
  const Int newWidth = input.getWidth(COMPONENT_Y) / factor;
  const Int newHeight = input.getHeight(COMPONENT_Y) / factor;
  output.createWithoutCUInfo(newWidth, newHeight, input.getChromaFormat(), true, s_padding, s_padding);

  const Pel* srcRow   =input.getAddr(COMPONENT_Y);
  const UInt srcStride=input.getStride(COMPONENT_Y);
        Pel *dstRow   =output.getAddr(COMPONENT_Y);
  const UInt dstStride=output.getStride(COMPONENT_Y);

  for (Int y = 0; y < newHeight; y++, srcRow+=factor*srcStride, dstRow+=dstStride)
  {
    const Pel *inRow      = srcRow;
    const Pel *inRowBelow = srcRow+srcStride;
          Pel *target     = dstRow;

    for (Int x = 0; x < newWidth; x++)
    {
      target[x] = (inRow[0] + inRowBelow[0] + inRow[1] + inRowBelow[1] + 2) >> 2;
      inRow += 2;
      inRowBelow += 2;
    }
  }
  output.extendPicBorder();
}

Int TEncTemporalFilter::motionErrorLuma(const TComPicYuv &orig,
                                        const TComPicYuv &buffer,
                                        const Int x,
                                        const Int y,
                                              Int dx,
                                              Int dy,
                                        const Int bs,
                                        const Int besterror = 8 * 8 * 1024 * 1024) const
{

  const Pel* origOrigin =orig.getAddr(COMPONENT_Y);
  const Int origStride  =orig.getStride(COMPONENT_Y);
  const Pel *buffOrigin =buffer.getAddr(COMPONENT_Y);
  const Int buffStride  =buffer.getStride(COMPONENT_Y);
  Int error = 0;// dx * 10 + dy * 10;
  if (((dx | dy) & 0xF) == 0)
  {
    dx /= s_motionVectorFactor;
    dy /= s_motionVectorFactor;
    for (Int y1 = 0; y1 < bs; y1++)
    {
      const Pel* origRowStart = origOrigin + (y+y1)*origStride + x;
      const Pel* bufferRowStart = buffOrigin + (y+y1+dy)*buffStride + (x+dx);
      for (Int x1 = 0; x1 < bs; x1 += 2)
      {
        Int diff = origRowStart[x1] - bufferRowStart[x1];
        error += diff * diff;
        diff = origRowStart[x1 + 1] - bufferRowStart[x1 + 1];
        error += diff * diff;
      }
      if (error > besterror)
      {
        return error;
      }
    }
  }
  else
  {
    const Int *xFilter = s_interpolationFilter[dx & 0xF];
    const Int *yFilter = s_interpolationFilter[dy & 0xF];
    Int tempArray[64 + 8][64];

    Int iSum, iBase;
    for (Int y1 = 1; y1 < bs + 7; y1++)
    {
      const Int yOffset = y + y1 + (dy >> 4) - 3;
      const Pel *sourceRow = buffOrigin + (yOffset)*buffStride + 0;
      for (Int x1 = 0; x1 < bs; x1++)
      {
        iSum = 0;
        iBase = x + x1 + (dx >> 4) - 3;
        const Pel *rowStart = sourceRow + iBase;

        iSum += xFilter[1] * rowStart[1];
        iSum += xFilter[2] * rowStart[2];
        iSum += xFilter[3] * rowStart[3];
        iSum += xFilter[4] * rowStart[4];
        iSum += xFilter[5] * rowStart[5];
        iSum += xFilter[6] * rowStart[6];

        tempArray[y1][x1] = iSum;
      }
    }

    const Pel maxSampleValue = (1<<m_internalBitDepth[CHANNEL_TYPE_LUMA])-1;
    for (Int y1 = 0; y1 < bs; y1++)
    {
      const Pel *origRow = origOrigin + (y+y1)*origStride + 0;
      for (Int x1 = 0; x1 < bs; x1++)
      {
        iSum = 0;
        iSum += yFilter[1] * tempArray[y1 + 1][x1];
        iSum += yFilter[2] * tempArray[y1 + 2][x1];
        iSum += yFilter[3] * tempArray[y1 + 3][x1];
        iSum += yFilter[4] * tempArray[y1 + 4][x1];
        iSum += yFilter[5] * tempArray[y1 + 5][x1];
        iSum += yFilter[6] * tempArray[y1 + 6][x1];

        iSum = (iSum + (1 << 11)) >> 12;
        iSum = iSum < 0 ? 0 : (iSum > maxSampleValue ? maxSampleValue : iSum);

        error += (iSum - origRow[x + x1]) * (iSum - origRow[x + x1]);
      }
      if (error > besterror)
      {
        return error;
      }
    }
  }
  return error;
}

Void TEncTemporalFilter::motionEstimationLuma(Array2D<MotionVector> &mvs, const TComPicYuv &orig, const TComPicYuv &buffer, const Int blockSize,
    const Array2D<MotionVector> *previous, const Int factor, const Bool doubleRes) const
{
  Int range = 5;
  const Int stepSize = blockSize;

  const Int origWidth  = orig.getWidth(COMPONENT_Y);
  const Int origHeight = orig.getHeight(COMPONENT_Y);

  for (Int blockY = 0; blockY + blockSize < origHeight; blockY += stepSize)
  {
    for (Int blockX = 0; blockX + blockSize < origWidth; blockX += stepSize)
    {
      MotionVector best;

      if (previous == NULL)
      {
        range = 8;
      }
      else
      {
        for (Int py = -2; py <= 2; py++)
        {
          Int testy = blockY / (2 * blockSize) + py;
          for (Int px = -2; px <= 2; px++)
          {
            Int testx = blockX / (2 * blockSize) + px;
            if ((testx >= 0) && (testx < origWidth / (2 * blockSize)) && (testy >= 0) && (testy < origHeight / (2 * blockSize)))
            {
              MotionVector old = previous->get(testx, testy);
              Int error = motionErrorLuma(orig, buffer, blockX, blockY, old.x * factor, old.y * factor, blockSize, best.error);
              if (error < best.error)
              {
                best.set(old.x * factor, old.y * factor, error);
              }
            }
          }
        }
      }
      MotionVector prevBest = best;
      for (Int y2 = prevBest.y / s_motionVectorFactor - range; y2 <= prevBest.y / s_motionVectorFactor + range; y2++)
      {
        for (Int x2 = prevBest.x / s_motionVectorFactor - range; x2 <= prevBest.x / s_motionVectorFactor + range; x2++)
        {
          Int error = motionErrorLuma(orig, buffer, blockX, blockY, x2 * s_motionVectorFactor, y2 * s_motionVectorFactor, blockSize, best.error);
          if (error < best.error)
          {
            best.set(x2 * s_motionVectorFactor, y2 * s_motionVectorFactor, error);
          }
        }
      }
      if (doubleRes)
      { // merge into one loop, probably with precision array (here [12, 3] or maybe [4, 1]) with setable number of iterations
        prevBest = best;
        Int doubleRange = 3 * 4;
        for (Int y2 = prevBest.y - doubleRange; y2 <= prevBest.y + doubleRange; y2 += 4)
        {
          for (Int x2 = prevBest.x - doubleRange; x2 <= prevBest.x + doubleRange; x2 += 4)
          {
            Int error = motionErrorLuma(orig, buffer, blockX, blockY, x2, y2, blockSize, best.error);
            if (error < best.error)
            {
              best.set(x2, y2, error);
            }

          }
        }

        prevBest = best;
        doubleRange = 3;
        for (Int y2 = prevBest.y - doubleRange; y2 <= prevBest.y + doubleRange; y2++)
        {
          for (Int x2 = prevBest.x - doubleRange; x2 <= prevBest.x + doubleRange; x2++)
          {
            Int error = motionErrorLuma(orig, buffer, blockX, blockY, x2, y2, blockSize, best.error);
            if (error < best.error)
            {
              best.set(x2, y2, error);
            }

          }
        }

      }
      mvs.get(blockX / stepSize, blockY / stepSize) = best;
    }
  }
}

Void TEncTemporalFilter::motionEstimation(Array2D<MotionVector> &mv, const TComPicYuv &orgPic, const TComPicYuv &buffer, const TComPicYuv &origSubsampled2, const TComPicYuv &origSubsampled4) const
{
  const Int width = m_sourceWidth;
  const Int height = m_sourceHeight;
  Array2D<MotionVector> mv_0(width / 16, height / 16);
  Array2D<MotionVector> mv_1(width / 16, height / 16);
  Array2D<MotionVector> mv_2(width / 16, height / 16);

  TComPicYuv bufferSub2;
  TComPicYuv bufferSub4;

  subsampleLuma(buffer, bufferSub2);
  subsampleLuma(bufferSub2, bufferSub4);

  motionEstimationLuma(mv_0, origSubsampled4, bufferSub4, 16);
  motionEstimationLuma(mv_1, origSubsampled2, bufferSub2, 16, &mv_0, 2);
  motionEstimationLuma(mv_2, orgPic, buffer, 16, &mv_1, 2);

  motionEstimationLuma(mv, orgPic, buffer, 8, &mv_2, 1, true);
}

Void TEncTemporalFilter::applyMotion(const Array2D<MotionVector> &mvs, const TComPicYuv &input, TComPicYuv &output) const
{
  static const Int lumaBlockSize=8;

  for(Int c=0; c< getNumberValidComponents(m_chromaFormatIDC); c++)
  {
    const ComponentID compID=(ComponentID)c;
    const Int csx=getComponentScaleX(compID, m_chromaFormatIDC);
    const Int csy=getComponentScaleY(compID, m_chromaFormatIDC);
    const Int blockSizeX = lumaBlockSize>>csx;
    const Int blockSizeY = lumaBlockSize>>csy;
    const Int height = input.getHeight(compID);
    const Int width  = input.getWidth(compID);

    const Pel maxValue = (1<<m_internalBitDepth[toChannelType(compID)])-1;

    const Pel *pSrcImage=input.getAddr(compID);
    const Int srcStride=input.getStride(compID);

          Pel *pDstImage=output.getAddr(compID);
          Int dstStride=output.getStride(compID);

    for (Int y = 0, blockNumY = 0; y + blockSizeY <= height; y += blockSizeY, blockNumY++)
    {
      for (Int x = 0, blockNumX = 0; x + blockSizeX <= width; x += blockSizeX, blockNumX++)
      {
        const MotionVector &mv = mvs.get(blockNumX,blockNumY);
        const Int dx = mv.x >> csx ;
        const Int dy = mv.y >> csy ;
        const Int xInt = mv.x >> (4+csx) ;
        const Int yInt = mv.y >> (4+csy) ;

        const Int *xFilter = s_interpolationFilter[dx & 0xf];
        const Int *yFilter = s_interpolationFilter[dy & 0xf]; // will add 6 bit.
        const Int numFilterTaps=7;
        const Int centreTapOffset=3;

        Int tempArray[lumaBlockSize + numFilterTaps][lumaBlockSize];

        for (Int by = 1; by < blockSizeY + numFilterTaps; by++)
        {
          const Int yOffset = y + by + yInt - centreTapOffset;
          const Pel *sourceRow = pSrcImage+yOffset*srcStride;
          for (Int bx = 0; bx < blockSizeX; bx++)
          {
            Int iBase = x + bx + xInt - centreTapOffset;
            const Pel *rowStart = sourceRow + iBase;

            Int iSum = 0;
            iSum += xFilter[1] * rowStart[1];
            iSum += xFilter[2] * rowStart[2];
            iSum += xFilter[3] * rowStart[3];
            iSum += xFilter[4] * rowStart[4];
            iSum += xFilter[5] * rowStart[5];
            iSum += xFilter[6] * rowStart[6];

            tempArray[by][bx] = iSum;
          }
        }

        Pel *pDstRow=pDstImage+y*dstStride;
        for (Int by = 0; by < blockSizeY; by++, pDstRow+=dstStride)
        {
          Pel *pDstPel=pDstRow+x;
          for (Int bx = 0; bx < blockSizeX; bx++, pDstPel++)
          {
            Int iSum = 0;

            iSum += yFilter[1] * tempArray[by + 1][bx];
            iSum += yFilter[2] * tempArray[by + 2][bx];
            iSum += yFilter[3] * tempArray[by + 3][bx];
            iSum += yFilter[4] * tempArray[by + 4][bx];
            iSum += yFilter[5] * tempArray[by + 5][bx];
            iSum += yFilter[6] * tempArray[by + 6][bx];

            iSum = (iSum + (1 << 11)) >> 12;
            iSum = iSum < 0 ? 0 : (iSum > maxValue ? maxValue : iSum);
            *pDstPel = iSum;
          }
        }
      }
    }
  }
}

Void TEncTemporalFilter::bilateralFilter(const TComPicYuv &orgPic,
                                         const std::deque<TemporalFilterSourcePicInfo> &srcFrameInfo,
                                               TComPicYuv &newOrgPic,
                                               Double overallStrength) const
{
  const int numRefs = Int(srcFrameInfo.size());
  std::vector<TComPicYuv> correctedPics(numRefs);
  for (Int i = 0; i < numRefs; i++)
  {
    correctedPics[i].createWithoutCUInfo( m_sourceWidth, m_sourceHeight, orgPic.getChromaFormat(), true, s_padding, s_padding );
    applyMotion(srcFrameInfo[i].mvs, srcFrameInfo[i].picBuffer, correctedPics[i]);
  }

  Int refStrengthRow = 2;
  if (numRefs == s_range*2)
  {
    refStrengthRow = 0;
  }
  else if (numRefs == s_range)
  {
    refStrengthRow = 1;
  }

  const Double lumaSigmaSq = (m_QP - s_sigmaZeroPoint) * (m_QP - s_sigmaZeroPoint) * s_sigmaMultiplier;
  const Double chromaSigmaSq = 30 * 30;
  
  for(Int c=0; c< getNumberValidComponents(m_chromaFormatIDC); c++)
  {
    const ComponentID compID=(ComponentID)c;
    const Int height = orgPic.getHeight(compID);
    const Int width  = orgPic.getWidth(compID);
    const Pel *srcPelRow = orgPic.getAddr(compID);
    const Int srcStride = orgPic.getStride(compID);
          Pel *dstPelRow = newOrgPic.getAddr(compID);
    const Int dstStride = newOrgPic.getStride(compID);
    const Double sigmaSq = isChroma(compID)? chromaSigmaSq : lumaSigmaSq;
    const Double weightScaling = overallStrength * (isChroma(compID) ? s_chromaFactor : 0.4);
    const Pel maxSampleValue = (1<<m_internalBitDepth[toChannelType(compID)])-1;
    const Double bitDepthDiffWeighting=1024.0 / (maxSampleValue+1);

    for (Int y = 0; y < height; y++, srcPelRow+=srcStride, dstPelRow+=dstStride)
    {
      const Pel *srcPel=srcPelRow;
            Pel *dstPel=dstPelRow;
      for (Int x = 0; x < width; x++, srcPel++, dstPel++)
      {
        const Int orgVal = (Int) *srcPel;
        Double temporalWeightSum = 1.0;
        Double newVal = (Double) orgVal;
        for (Int i = 0; i < numRefs; i++)
        {
          const Pel *pCorrectedPelPtr=correctedPics[i].getAddr(compID)+(y*correctedPics[i].getStride(compID)+x);
          const Int refVal = (Int) *pCorrectedPelPtr;
          Double diff = (Double)(refVal - orgVal);
          diff *= bitDepthDiffWeighting;
          Double diffSq = diff * diff;
          const Int index = std::min(1, std::abs(srcFrameInfo[i].origOffset) - 1);
          const Double weight = weightScaling * s_refStrengths[refStrengthRow][index] * exp(-diffSq / (2 * sigmaSq));
          newVal += weight * refVal;
          temporalWeightSum += weight;
        }
        newVal /= temporalWeightSum;
        Pel sampleVal = (Pel)round(newVal);
        sampleVal=(sampleVal<0?0 : (sampleVal>maxSampleValue ? maxSampleValue : sampleVal));
        *dstPel = sampleVal;
      }
    }
  }
}

//! \}
