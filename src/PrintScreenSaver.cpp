#include "pch.hpp"

//#define SAVE_BMP_TOO

namespace {
    constexpr size_t MAX_TIMESTAMP_LEN = 80;
}

void PrintScreenSaver::Init()
{
}

void PrintScreenSaver::SaveScreenshot()
{
    if(screenshot_future.valid())
        screenshot_future.get();

    screenshot_future = std::async(std::launch::async, &PrintScreenSaver::DoSave, this);
}

void PrintScreenSaver::FormatTimestamp(char* buf, size_t len)
{
    if(len < 5)
        return;

    time_t rawtime;
    struct tm* timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(buf, len - 5, timestamp_format.c_str(), timeinfo);
    strncat(buf, ".png", 5);
}

void PrintScreenSaver::DoSave()
{
    const auto t1 = std::chrono::steady_clock::now();
#ifdef _WIN32
    OpenClipboard(nullptr);
    HGLOBAL ClipboardDataHandle = static_cast<HGLOBAL>(GetClipboardData(CF_DIB));
    if(!ClipboardDataHandle)
    {
        // Clipboard object is not a DIB, and is not auto-convertible to DIB
        CloseClipboard();
        return;
    }

    BITMAPINFOHEADER* BitmapInfoHeader = static_cast<BITMAPINFOHEADER*>(GlobalLock(ClipboardDataHandle));
    if(!BitmapInfoHeader)
    {
        CloseClipboard();
        return;
    }

    const SIZE_T ClipboardDataSize = GlobalSize(ClipboardDataHandle);
    if(ClipboardDataSize < sizeof(BITMAPINFOHEADER))
    {
        // Malformed data — CF_DIB mandates a BITMAPINFO struct
        GlobalUnlock(ClipboardDataHandle);
        CloseClipboard();
        return;
    }

    const INT PixelDataOffset = GetPixelDataOffsetForPackedDIB(BitmapInfoHeader);
    const size_t TotalBitmapFileSize = sizeof(BITMAPFILEHEADER) + ClipboardDataSize;

    BITMAPFILEHEADER BitmapFileHeader = {};
    BitmapFileHeader.bfType    = 0x4D42; // 'BM' signature
    BitmapFileHeader.bfSize    = static_cast<DWORD>(TotalBitmapFileSize);
    BitmapFileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + PixelDataOffset;

    // Copy all clipboard data into local buffers before releasing the clipboard
    std::vector<unsigned char> clipboard_bmp;
    clipboard_bmp.reserve(TotalBitmapFileSize);
    std::copy(reinterpret_cast<const unsigned char*>(&BitmapFileHeader),
              reinterpret_cast<const unsigned char*>(&BitmapFileHeader) + sizeof(BITMAPFILEHEADER),
              std::back_inserter(clipboard_bmp));
    std::copy(reinterpret_cast<const unsigned char*>(BitmapInfoHeader),
              reinterpret_cast<const unsigned char*>(BitmapInfoHeader) + ClipboardDataSize,
              std::back_inserter(clipboard_bmp));

    GlobalUnlock(ClipboardDataHandle);
    CloseClipboard();

    char buf[MAX_TIMESTAMP_LEN];
    FormatTimestamp(buf, MAX_TIMESTAMP_LEN);

    std::vector<unsigned char> png;
    std::vector<unsigned char> bmp_to_encode;
    unsigned w_ = 0, h_ = 0;
    const unsigned decode_error = decodeBMP(bmp_to_encode, w_, h_, clipboard_bmp);
    if(decode_error != 0)
    {
        LOG(LogLevel::Error, "Failed to decode BMP from clipboard, error: {}", decode_error);
        MyFrame* frame = static_cast<MyFrame*>(wxGetApp().GetTopWindow());
        std::lock_guard lock(frame->mtx);
        frame->pending_msgs.push_back({ static_cast<uint8_t>(PopupMsgIds::ScreenshotSaveFailed) });
        return;
    }

    unsigned long error_code = lodepng::encode(png, bmp_to_encode, w_, h_, LCT_RGBA, 8);

    std::string save_path = screenshot_path.string() + "\\" + buf;
    if(!error_code)
        error_code = lodepng::save_file(png, save_path.c_str());

    const auto t2 = std::chrono::steady_clock::now();
    const int64_t dif = std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count();

    if(!error_code)
        LOG(LogLevel::Notification, "Image saved to {}", save_path);
    else
        LOG(LogLevel::Error, "Failed to save image from the clipboard!");

    MyFrame* frame = static_cast<MyFrame*>(wxGetApp().GetTopWindow());
    {
        std::lock_guard lock(frame->mtx);
        if(!error_code)
            frame->pending_msgs.push_back({ static_cast<uint8_t>(PopupMsgIds::ScreenshotSaved), dif, std::move(save_path) });
        else
            frame->pending_msgs.push_back({ static_cast<uint8_t>(PopupMsgIds::ScreenshotSaveFailed) });
    }
#endif
}

#ifdef _WIN32
// Returns the offset, in bytes, from the start of the BITMAPINFO, to the start of the pixel data array, for a packed DIB.
INT PrintScreenSaver::GetPixelDataOffsetForPackedDIB(const BITMAPINFOHEADER* BitmapInfoHeader)
{
    INT OffsetExtra = 0;

    if(BitmapInfoHeader->biSize == sizeof(BITMAPINFOHEADER) /* 40 */)
    {
        // This is the common BITMAPINFOHEADER type. In this case, there may be bit masks following the BITMAPINFOHEADER
        // and before the actual pixel bits (does not apply if bitmap has <= 8 bpp)
        if(BitmapInfoHeader->biBitCount > 8)
        {
            if(BitmapInfoHeader->biCompression == BI_BITFIELDS)
            {
                OffsetExtra += 3 * sizeof(RGBQUAD);
            }
            else if(BitmapInfoHeader->biCompression == 6 /* BI_ALPHABITFIELDS */)
            {
                // Not widely supported, but valid.
                OffsetExtra += 4 * sizeof(RGBQUAD);
            }
        }
    }

    if(BitmapInfoHeader->biClrUsed > 0)
    {
        // We have no choice but to trust this value.
        OffsetExtra += BitmapInfoHeader->biClrUsed * sizeof(RGBQUAD);
    }
    else
    {
        // In this case, the color table contains the maximum number for the current bit count (0 if > 8bpp)
        if(BitmapInfoHeader->biBitCount <= 8)
        {
            // 1bpp: 2
            // 4bpp: 16
            // 8bpp: 256
            OffsetExtra += sizeof(RGBQUAD) << BitmapInfoHeader->biBitCount;
        }
    }

    return BitmapInfoHeader->biSize + OffsetExtra;
}

//returns 0 if all went ok, non-0 if error
//output image is always given in RGBA (with alpha channel), even if it's a BMP without alpha channel
unsigned PrintScreenSaver::decodeBMP(std::vector<unsigned char>& image, unsigned& w, unsigned& h, const std::vector<unsigned char>& bmp)
{
    constexpr unsigned MINHEADER = 54; // minimum BMP header size

    if(bmp.size() < MINHEADER) return 4;
    if(bmp[0] != 'B' || bmp[1] != 'M') return 1; //It's not a BMP file if it doesn't start with marker 'BM'
    unsigned pixeloffset = bmp[10] + 256 * bmp[11]; //where the pixel data starts
    //read width and height from BMP header
    w = bmp[18] + bmp[19] * 256;
    h = static_cast<int16_t>(bmp[22] + bmp[23] * 256);
    //read number of channels from BMP header
    if(bmp[28] != 24 && bmp[28] != 32) return 2; //only 24-bit and 32-bit BMPs are supported.
    unsigned numChannels = bmp[28] / 8;

    //The amount of scanline bytes is width of image times channels, with extra bytes added if needed
    //to make it a multiple of 4 bytes.
    unsigned scanlineBytes = w * numChannels;
    if(scanlineBytes % 4 != 0) scanlineBytes = (scanlineBytes / 4) * 4 + 4;

    unsigned dataSize = scanlineBytes * h;
    if(bmp.size() < dataSize + pixeloffset) return 3; //BMP file too small to contain all pixels

    image.resize(w * h * 4);

    /*
    There are 3 differences between BMP and the raw image buffer for LodePNG:
    -it's upside down
    -it's in BGR instead of RGB format (or BRGA instead of RGBA)
    -each scanline has padding bytes to make it a multiple of 4 if needed
    The 2D for loop below does all these 3 conversions at once.
    */
    for(unsigned y = 0; y < h; y++)
        for(unsigned x = 0; x < w; x++) {
            //pixel start byte position in the BMP
            unsigned bmpos = pixeloffset + (h - y - 1) * scanlineBytes + numChannels * x;
            //pixel start byte position in the new raw image
            unsigned newpos = 4 * y * w + 4 * x;
            if(numChannels == 3) {
                image[newpos + 0] = bmp[bmpos + 2]; //R
                image[newpos + 1] = bmp[bmpos + 1]; //G
                image[newpos + 2] = bmp[bmpos + 0]; //B
                image[newpos + 3] = 255;            //A
            }
            else {
                image[newpos + 0] = bmp[bmpos + 2]; //R
                image[newpos + 1] = bmp[bmpos + 1]; //G
                image[newpos + 2] = bmp[bmpos + 0]; //B
                image[newpos + 3] = bmp[bmpos + 3]; //A
            }
        }
    return 0;
}
#endif