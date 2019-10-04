#include <string>
#include <sstream>
#include <algorithm>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#define DEBUG

unsigned int position_offset = 0, name_offset = 0, ping_offset = 0, time_offset = 0, car_offset = 0, best_lap_offset = 0;

struct Result {
    unsigned char position;
    std::string raw_position;
    std::string name;
    std::string time;
    std::string car;
    std::string best_lap;
    bool dnf;
};

// TODO: allocate dynamically instead of using global variables
Result results[25];

bool is_valid_time_digit(char c)
{
    switch (c) {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case ':':
        case '.':
            return false;
        default:
            return true;
    }
}

std::string clean_time(std::string &time)
{
    if (time.compare("DNF") == 0 || time.compare("ONF") == 0) {
        return std::string("DNF");
    }
    time.erase(std::remove_if(time.begin(), time.end(), &is_valid_time_digit), time.end());
    return time;
}

bool process_line(int index, tesseract::ResultIterator* ri) 
{
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
    int word_index = 0;
    Result *result = &results[index];
    do {
        const char* word = ri->GetUTF8Text(level);
        float conf = ri->Confidence(level);
        int x1, y1, x2, y2;
        ri->BoundingBox(level, &x1, &y1, &x2, &y2);
        std::string token = word;
        // TODO: Split out to process_header function and fix for localized strings
        if (index == 0) {
            if (token.compare("POS") == 0 || word_index == 0) {
                position_offset = x1;
            } else if (token.compare("NAME") == 0 ) {
                name_offset = x1;
            } else if (token.compare("PING") == 0) { // multiplayer only
                ping_offset = x1;
            } else if (token.compare("CAR") == 0 ) {
                car_offset = x1;
            } else if (token.compare("TIME") == 0 ) {
                time_offset = x1;
            } else if (token.compare("BEST") == 0 ) {
                best_lap_offset = x1;
            }
        }
        if (word_index == 0) {
            result->raw_position = std::string(word);
            result->position = std::atoi(word);
        } else if (result->name.empty() && x1 >= name_offset - 10) {
            result->name = token;
        } else if (!result->name.empty() && x2 < ping_offset) {
            result->name.append(" ");
            result->name.append(token);
        } else if (x1 >= car_offset - 10 && x2 < time_offset) {
            if (!result->car.empty()) {
                result->car.append(" ");
            }
            result->car.append(token);
        } else if (result->time.empty() && word_index >= 1 && x1 >= time_offset - 10) {
            result->time = clean_time(token);
        } else if (result->best_lap.empty() && word_index >= 1 && x1 >= best_lap_offset - 10) {
            result->best_lap = clean_time(token);
        }
        delete[] word;
        ++word_index;
    } while (ri->Next(level) && !ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE));
#ifdef DEBUG
    printf("Position %d (%s) [Offset=%d]: NAME=%s CAR=%s TIME=%s LAP=%s\n", result->position, result->raw_position.c_str(), result->position - index, result->name.c_str(), result->car.c_str(), result->time.c_str(), result->best_lap.c_str());
#endif
    return ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE);
}

/* Rewrite positions by finding the most common offset (e.g. 0 if viewing
 * positions 1-16, 4 if viewing 5-20). This will introduce errors if whole rows
 * are not detected by the OCR engine. */
void clean_positions()
{
    unsigned char offsets[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int index = 1; index < 25; ++index) {
        Result *res = &results[index];
        int offset = res->position - index;
        if (offset >= 0 && offset < 10) {
            offsets[offset] += 1;
        }
    }
    int majority_offset = std::max_element(offsets, offsets + 10) - offsets;
    for (int index = 1; index < 25; ++index) {
        Result *res = &results[index];
#ifdef DEBUG
        if (res->position != index + majority_offset) {
            printf("Changing position %d to %d at index %d\n", res->position, index + majority_offset, index);
        }
#endif
        res->position = index + majority_offset;
    }
}

void convert(char *filename, tesseract::TessBaseAPI *api)
{
    l_int32 width, height;
    char *outText;
    int index = 0;
    pixReadHeader(filename, nullptr, &width, &height, nullptr, nullptr, nullptr); 
    float aspect_ratio = (float)width / height;
    float crop_factor = 3.0;
    // Crop empty spaces for ultrawide resolutions
    if (aspect_ratio > 2.3 && aspect_ratio < 2.5) {
        crop_factor = 2.66;
    }
    // Crop image to results section
    l_int32 left = (l_int32)(width / crop_factor);
    l_int32 top = height / 5;
    l_int32 region_width = (l_int32)(width - left - (width / (int)(crop_factor * crop_factor * crop_factor / 3)));
    l_int32 region_height = height - top - (height / 12);
    Box *box = boxCreate(left, top, region_width, region_height);
    Pix *image = pixRead(filename);
    Pix *cropped_image = pixClipRectangle(image, box, nullptr);
    // Optimize image for OCR (negative colors, contrast enhancement, background removal)
    pixInvert(cropped_image, cropped_image);
    Pix *grey_image = pixConvertRGBToLuminance(cropped_image);
    pixContrastTRC(grey_image, grey_image, 0.6);
    Pix *remove_bg = pixBackgroundNormSimple(grey_image, NULL, NULL);
    Pix *mono_image = pixCleanBackgroundToWhite(remove_bg, NULL, NULL, 1.0, 70, 190);
    // Cut out player logos because it confuses the recognition of names
    Box *logos = boxCreate(region_width / 14, 0, region_width / 9 - region_width / 14, region_height);
    pixSetInRect(mono_image, logos);
    // Blank out line separators
    for (int row = 1; row <= 16; ++row) {
        Box *separator = boxCreate(0, (height / 30) + (height / 24 * row), region_width, 10);
        pixSetInRect(mono_image, separator);
        boxDestroy(&separator);
    }
#ifdef DEBUG
    pixWritePng("preprocessed.png", mono_image, 0);
#endif
    
    api->SetImage(mono_image);
    api->Recognize(0);
    tesseract::ResultIterator* ri = api->GetIterator();
    if (ri != 0) {
        while (process_line(index, ri)) {
            ++index;
        }
    }

    pixDestroy(&mono_image);
    pixDestroy(&remove_bg);
    pixDestroy(&grey_image);
    pixDestroy(&cropped_image);
    pixDestroy(&image);
    boxDestroy(&logos);
    boxDestroy(&box);
}

int main(int argc, char *argv [])
{
    tesseract::TessBaseAPI *api = new tesseract::TessBaseAPI();
    if (api->Init(NULL, "eng")) {
        fprintf(stderr, "Could not initialize tesseract.\n");
        exit(1);
    }
    for (int i = 1; i < argc; ++i) {
        char *filename = argv[i];
#ifdef DEBUG
        printf("Processing %d / %d %s ...\n", i, argc-1, filename);
#endif
        convert(filename, api);
        clean_positions();
        printf("Position,Name,Car,Time,Best Lap\n");
        for (int index = 1; index < 24; ++index) {
            Result *res = &results[index];
            if (!res->name.empty()) {
                printf("%d,%s,%s,%s,%s\n", res->position, res->name.c_str(), res->car.c_str(), res->time.c_str(), res->best_lap.c_str());
            }
        }
    }
    api->End();
    return 0;
}
