#ifndef IMAGEPROCESSINGUTILS_H
#define IMAGEPROCESSINGUTILS_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <cstring>
#include <type_traits>
#include <limits>
#include <stdexcept>
#include <string>

enum class ImageProcessingTypes;

class ImageProcessingDescriptor {
public:
    virtual ~ImageProcessingDescriptor() {;}
    virtual ImageProcessingTypes getType() const = 0;
};

class IPDRotateCW : public ImageProcessingDescriptor {
public:
    ImageProcessingTypes getType() const override;
};

class IPDRotateCCW : public ImageProcessingDescriptor {
public:
    ImageProcessingTypes getType() const override;
};

class IPDFlipHorizontal : public ImageProcessingDescriptor {
public:
    ImageProcessingTypes getType() const override;
};

class IPDFlipVertical : public ImageProcessingDescriptor {
public:
    ImageProcessingTypes getType() const override;
};

class IPDBin : public ImageProcessingDescriptor {
public:
    IPDBin(int binFactorRHS) : binFactor(binFactorRHS) {}
    ImageProcessingTypes getType() const override;
    int binFactor;
};

class IPDCrop : public ImageProcessingDescriptor {
public:
    // nRows and nCols are the dimensions of the cropped image: the number of rows
    // (height) and columns (width), respectively.
    IPDCrop(size_t nRowsRHS, size_t nColsRHS) : nRows(nRowsRHS), nCols(nColsRHS) {}
    ImageProcessingTypes getType() const override;
    size_t nRows, nCols;
};

class AcquiredImage; // Forward declaration

AcquiredImage ProcessImage(const AcquiredImage& inputImage, const std::vector<std::shared_ptr<ImageProcessingDescriptor>>& processingDescriptors);

// All kernels below use row-major storage (index = row * nCols + col): nRows is the
// number of rows (the image height, the slow axis), nCols the number of columns (the
// image width, the fast axis). The pixel data varies fastest along the columns. This
// matches the layout delivered by the camera SDKs and expected by the consumers of
// the plugin API.

template <typename T>
void RotateCW(const T* image, size_t nRows, size_t nCols, T* rotatedImage) {
    // Clockwise rotation: image(r, c) -> rotated(c, nRows - 1 - r).
    // The output image has nCols rows and nRows columns.
    for (size_t r = 0; r < nRows; ++r) {
        for (size_t c = 0; c < nCols; ++c) {
            rotatedImage[c * nRows + (nRows - 1 - r)] = image[r * nCols + c];
        }
    }
}

template <typename T>
void RotateCCW(const T* image, size_t nRows, size_t nCols, T* rotatedImage) {
    // Counterclockwise rotation: image(r, c) -> rotated(nCols - 1 - c, r).
    // The output image has nCols rows and nRows columns.
    for (size_t r = 0; r < nRows; ++r) {
        for (size_t c = 0; c < nCols; ++c) {
            rotatedImage[(nCols - 1 - c) * nRows + r] = image[r * nCols + c];
        }
    }
}

template <typename T>
void FlipHorizontal(const T* image, size_t nRows, size_t nCols, T* flippedImage) {
    // Flip about the horizontal axis: the rows are reversed.
    for (size_t r = 0; r < nRows; ++r) {
        for (size_t c = 0; c < nCols; ++c) {
            flippedImage[r * nCols + c] = image[(nRows - 1 - r) * nCols + c];
        }
    }
}

template <typename T>
void FlipVertical(const T* image, size_t nRows, size_t nCols, T* flippedImage) {
    // Flip about the vertical axis: the columns are reversed.
    for (size_t r = 0; r < nRows; ++r) {
        for (size_t c = 0; c < nCols; ++c) {
            flippedImage[r * nCols + c] = image[r * nCols + (nCols - 1 - c)];
        }
    }
}

template <typename T>
void CropImage(const T* image, size_t nRows, size_t nCols, size_t outputNRows, size_t outputNCols, T* croppedImage) {
    // Centered crop to outputNRows x outputNCols.
    size_t rowOffset = (nRows - outputNRows) / 2;
    size_t colOffset = (nCols - outputNCols) / 2;

    for (size_t r = 0; r < outputNRows; ++r) {
        for (size_t c = 0; c < outputNCols; ++c) {
            croppedImage[r * outputNCols + c] = image[(r + rowOffset) * nCols + (c + colOffset)];
        }
    }
}

template <typename T>
void BinImage(const T* image, size_t nRows, size_t nCols, T* binnedImage, const int binFactor) {
    if (binFactor > 32) {
        throw std::logic_error(std::string("unsupported binning factor"));
    }
    if (binFactor == 1) {
        std::memcpy(binnedImage, image, nRows * nCols * sizeof(T));
        return;
    }

    int nRowsOutput = nRows / binFactor;
    int nColsOutput = nCols / binFactor;

    T* outputPtr = binnedImage;

    using AccumType = std::conditional_t<std::is_floating_point_v<T>, double, std::uint32_t>;
    AccumType maxVal = static_cast<AccumType>(std::numeric_limits<T>::max());

    for (int row = 0; row < nRowsOutput; row += 1) {
        const T* inputPtr = image + (row * binFactor) * nCols;
        for (int col = 0; col < nColsOutput; col += 1) {
            AccumType accum = 0;
            for (int bRow = 0; bRow < binFactor; bRow += 1) {
                const T* blockRowPtr = inputPtr + bRow * nCols;
                for (int bCol = 0; bCol < binFactor; bCol += 1) {
                    accum += static_cast<AccumType>(blockRowPtr[bCol]);
                }
            }
            if constexpr (std::is_integral_v<T>) {
                *outputPtr = static_cast<T>((accum > maxVal) ? maxVal : accum);
            } else {
                *outputPtr = static_cast<T>(accum);
            }
            outputPtr += 1;
            inputPtr += binFactor;
        }
    }
}

#endif
