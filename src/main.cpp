#include <iostream>
#include <set>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdint>
#include <algorithm>
#include <numeric>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image.h"
#include "stbi_image_write.h"

auto get_filenames(const std::filesystem::path& path) -> std::vector<std::string>
{
    namespace stdfs = std::filesystem;

    std::vector<std::string> filenames;

    // http://en.cppreference.com/w/cpp/experimental/fs/directory_iterator
    const stdfs::directory_iterator end{};

    for (stdfs::directory_iterator iter{ path.string() }; iter != end; ++iter)
    {
        // http://en.cppreference.com/w/cpp/experimental/fs/is_regular_file 
        if (stdfs::is_regular_file(*iter)) // comment out if all names (names of directories tc.) are required
            filenames.push_back(iter->path().string());
    }

    return filenames;
}

auto splitpath(const std::string& str, const std::set<char>& delimiters) -> std::vector<std::string>
{
    std::vector<std::string> result;

    char const* pch = str.c_str();
    char const* start = pch;
    for (; *pch; ++pch)
    {
        if (delimiters.find(*pch) != delimiters.end())
        {
            if (start != pch)
            {
                std::string str(start, pch);
                result.push_back(str);
            } else
            {
                result.push_back("");
            }
            start = pch + 1;
        }
    }
    result.push_back(start);

    return result;
}

auto get_timestamp(const std::string& file) -> unsigned long long
{
    std::set<char> delims{ '\\', '/' };         // path delimiters on windows and unix

    std::vector<std::string> path = splitpath(file, delims);

    auto fileName = path.back();

    size_t lastindex = fileName.find_last_of(".");
    fileName = fileName.substr(0, lastindex);

    auto timeString = fileName.substr(fileName.find("-") + 1);

    unsigned long long timestamp = std::stoull(timeString);
    return timestamp;
}

auto get_frame_id(const std::string& file) -> unsigned long
{
    std::set<char> delims{ '\\', '/' };         // path delimiters on windows and unix

    std::vector<std::string> path = splitpath(file, delims);

    auto fileName = path.back();

    size_t lastindex = fileName.find_last_of(".");
    fileName = fileName.substr(0, lastindex);

    auto timeString = fileName.substr(0, fileName.find("-"));

    unsigned long index = std::stoul(timeString);
    return index;
}

struct SUN3D_ImageInfo
{
    std::string path;
    unsigned long long timestamp;
    unsigned long index;
    enum Type { COLOR, DEPTH, ANY } type;
};

struct SUN3D_FrameInfo
{
    SUN3D_ImageInfo colorImage;
    SUN3D_ImageInfo depthImage;
};

auto process_SUN3D_folder(const std::string& dir, SUN3D_ImageInfo::Type type = SUN3D_ImageInfo::Type::ANY) -> std::vector<SUN3D_ImageInfo>
{
    auto files = get_filenames(dir);
    std::vector<SUN3D_ImageInfo> images;
    images.reserve(files.size());

    for (const auto& file : files) {
        SUN3D_ImageInfo image;
        image.path = file;
        image.timestamp = get_timestamp(file);
        image.index = get_frame_id(file);
        image.type = type;
        images.push_back(image);
    }
    return images;
}

struct CalibrationData
{
    float intrinsics[4][4] = { 0 };
    float extrinsics[4][4] = { 0 };
};

struct MetaData
{
    enum COMPRESSION_TYPE_COLOR
    {
        TYPE_COLOR_UNKNOWN = -1,
        TYPE_RAW = 0,
        TYPE_PNG = 1,
        TYPE_JPEG = 2
    };
    enum COMPRESSION_TYPE_DEPTH
    {
        TYPE_DEPTH_UNKNOWN = -1,
        TYPE_RAW_USHORT = 0,
        TYPE_ZLIB_USHORT = 1,
        TYPE_OCCI_USHORT = 2
    };

    COMPRESSION_TYPE_COLOR colorCompressionType;
    COMPRESSION_TYPE_DEPTH depthCompressionType;

    unsigned int colorWidth;
    unsigned int colorHeight;
    unsigned int depthWidth;
    unsigned int depthHeight;
    float depthShift = 1000.0f;
};

struct RGBDFrame
{
    float cameraToWorld[4][4] = { 0 };
    std::uint64_t timeStampColor;
    std::uint64_t timeStampDepth;
    std::uint64_t colorSizeBytes;
    std::uint64_t depthSizeBytes;
    unsigned char* colorCompressed;
    unsigned char* depthCompressed;
};

int main(int argc, char* argv[])
{
    std::string sun3d_dir = "D:/Uni/ToFML/Datasets/SUN3D/brown_bm_1/brown_bm_1/";
    std::string outFile = "output.sens";
    int startFrame = 0;
    int endFrame = 0;
#ifndef _DEBUG
    if (argc >= 2) sun3d_dir = std::string(argv[1]);
    else {
        std::cout << "Usage: .\\SUN3DtoSens <path/to/sun3d/root/dir> <outputfile>" << std::endl;
        return 0;
    }
    if (argc >= 3) {
        outFile = std::string(argv[2]);
    }
    if (argc >= 4) {
        startFrame = std::stoi(argv[3]);
    }
    if (argc >= 5) {
        endFrame = std::stoi(argv[4]);
    }
#endif

    if (endFrame == 0) {
        std::cout << "Specify endframe" << std::endl;
        return 0;
    }
    std::string color_dir = sun3d_dir + "/image/";
    std::string depth_dir = sun3d_dir + "/depth/";

    auto colorImages = process_SUN3D_folder(color_dir, SUN3D_ImageInfo::COLOR);
    auto depthImages = process_SUN3D_folder(depth_dir, SUN3D_ImageInfo::DEPTH);

    // search for closest depth image for each color image and combine both to frames
    std::vector<SUN3D_FrameInfo> sun3dFrames;
    sun3dFrames.reserve(colorImages.size());
    for (size_t i = 0; i < colorImages.size(); i++) {
        unsigned long long minDiff = -1;
        size_t minDiffIndex = 0;
        auto colorTimestamp = colorImages.at(i).timestamp;
        for (size_t j = 0; j < depthImages.size(); j++) {
            auto depthTimestamp = depthImages.at(j).timestamp;
            auto diff = colorTimestamp < depthTimestamp ?
                depthTimestamp - colorTimestamp : colorTimestamp - depthTimestamp;
            if (diff < minDiff) {
                minDiff = diff;
                minDiffIndex = j;
            } else if (j == minDiffIndex + 1) {     // we are only getting further away now because vectors are sorted
                break;
            }
        }
        SUN3D_FrameInfo frame;
        frame.colorImage = colorImages.at(i);
        frame.depthImage = depthImages.at(minDiffIndex);
        sun3dFrames.push_back(frame);
    }

    // read meta data
    int depthWidth, depthHeight, colorWidth, colorHeight, channels;
    {
        unsigned char* colorImage = stbi_load(sun3dFrames.at(0).colorImage.path.c_str(), &colorWidth, &colorHeight, &channels, 0);
        unsigned short* depthImage = stbi_load_16(sun3dFrames.at(0).depthImage.path.c_str(), &depthWidth, &depthHeight, &channels, 0);
        stbi_image_free(colorImage);
        stbi_image_free(depthImage);
    }

    // read intrinsics
    CalibrationData calibrationData;
    std::ifstream infile;
    infile.open(sun3d_dir + "/intrinsics.txt");
    if (!infile) {
        std::cerr << "Could not open intrinsics file" << std::endl;
        std::cin >> new char[0];
        return -1;
    }
    infile >> calibrationData.intrinsics[0][0] >> calibrationData.intrinsics[0][1] >> calibrationData.intrinsics[0][2];
    infile >> calibrationData.intrinsics[1][0] >> calibrationData.intrinsics[1][1] >> calibrationData.intrinsics[1][2];
    infile >> calibrationData.intrinsics[2][0] >> calibrationData.intrinsics[2][1] >> calibrationData.intrinsics[2][2];
    calibrationData.intrinsics[3][3] = 1.0f;
    infile.close();

    calibrationData.extrinsics[0][0] = calibrationData.extrinsics[1][1] = calibrationData.extrinsics[2][2] = calibrationData.extrinsics[3][3] = 1.0f;
    
    MetaData metaData{
        MetaData::COMPRESSION_TYPE_COLOR::TYPE_JPEG,
        MetaData::COMPRESSION_TYPE_DEPTH::TYPE_ZLIB_USHORT,
        colorWidth, colorHeight,
        depthWidth, depthHeight,
        1000.0f
    };

    unsigned int version_number = 4;
    std::string sensorName = "Unknown";
    std::uint64_t strLen = sensorName.size();

    std::ofstream outfile(outFile, std::ios::out | std::ios::binary);
    // write Sens header
    outfile.write((const char*)&version_number, sizeof(unsigned int));
    outfile.write((const char*)&strLen, sizeof(std::uint64_t));
    outfile.write((const char*)&sensorName[0], strLen * sizeof(char));
    outfile.write((const char*)&calibrationData.intrinsics, 16 * sizeof(float));
    outfile.write((const char*)&calibrationData.extrinsics, 16 * sizeof(float));
    outfile.write((const char*)&calibrationData.intrinsics, 16 * sizeof(float));
    outfile.write((const char*)&calibrationData.extrinsics, 16 * sizeof(float));
    outfile.write((const char*)&metaData.colorCompressionType, sizeof(MetaData::COMPRESSION_TYPE_COLOR));
    outfile.write((const char*)&metaData.depthCompressionType, sizeof(MetaData::COMPRESSION_TYPE_DEPTH));
    outfile.write((const char*)&metaData.colorWidth, sizeof(unsigned int));
    outfile.write((const char*)&metaData.colorHeight, sizeof(unsigned int));
    outfile.write((const char*)&metaData.depthWidth, sizeof(unsigned int));
    outfile.write((const char*)&metaData.depthHeight, sizeof(unsigned int));
    outfile.write((const char*)&metaData.depthShift, sizeof(float));
    // write Sens frames

    //std::uint64_t numFrames = sun3dFrames.size();
    std::uint64_t numFrames = sun3dFrames.size();


    if (endFrame != 0) {
        if (endFrame < numFrames) {
            numFrames = endFrame - startFrame;
        } else {
            std::cerr << "Number of frames exceeds frames in folder!" << std::endl;
            return 1;
        }
    }

    outfile.write((const char*)&numFrames, sizeof(std::uint64_t));

    outfile.flush();
    int j = 0;
    for (int i = startFrame; i < endFrame; i++) {
        SUN3D_ImageInfo colorInfo = sun3dFrames.at(i).colorImage;
        SUN3D_ImageInfo depthInfo = sun3dFrames.at(i).depthImage;

        infile.open(colorInfo.path, std::ios::in | std::ios::binary | std::ios::ate);
        size_t colorLength = infile.tellg();
        infile.seekg(0, infile.beg);
        unsigned char* colorImage = new unsigned char[colorLength];
        infile.read((char*)colorImage, colorLength);
        if (!infile) {
            std::cerr << "Error: only " << infile.gcount() << "of " << colorLength << " bytes could be read" << std::endl;
        }
        infile.close();
        int channels, w, h;
        unsigned short* depthImage = stbi_load_16(depthInfo.path.c_str(), &w, &h, &channels, 1);

        for (int j = 0; j < depthWidth * depthHeight; j++) {
            depthImage[j] = (depthImage[j] >> 3) | (depthImage[j] << 13);
        }

        // compress depth
        int out_len = 0;
        int quality = 8;
        int n = 2;
        unsigned char* tmpBuff = (unsigned char*)std::malloc((w*n + 1)*h);
        std::memcpy(tmpBuff, depthImage, w * h * sizeof(unsigned short));
        unsigned char* depthCompressed;
        depthCompressed = stbi_zlib_compress(tmpBuff, w * h * sizeof(unsigned short), &out_len, quality);
        std::free(tmpBuff);

        RGBDFrame frame;
        frame.colorCompressed = colorImage;
        frame.depthCompressed = depthCompressed;
        frame.colorSizeBytes = colorLength;
        frame.depthSizeBytes = out_len;
        frame.timeStampColor = colorInfo.timestamp;
        frame.timeStampDepth = depthInfo.timestamp;
        std::memset(frame.cameraToWorld, -std::numeric_limits<float>::infinity(), 16*sizeof(float)); // default value

        outfile.write((const char*)&frame.cameraToWorld, 16 * sizeof(float));
        outfile.write((const char*)&frame.timeStampColor, sizeof(std::uint64_t));
        outfile.write((const char*)&frame.timeStampDepth, sizeof(std::uint64_t));
        outfile.write((const char*)&frame.colorSizeBytes, sizeof(std::uint64_t));
        outfile.write((const char*)&frame.depthSizeBytes, sizeof(std::uint64_t));
        outfile.write((const char*)frame.colorCompressed, frame.colorSizeBytes);
        outfile.write((const char*)frame.depthCompressed, frame.depthSizeBytes);

        stbi_image_free(depthCompressed);
        stbi_image_free(depthImage);
        delete[] colorImage;

        printf("%d of %d frames written\r", j, numFrames);
        j++;
    }

    char zero = 0;

    outfile.write(&zero, 1);
    outfile.close();
    return 0;
}