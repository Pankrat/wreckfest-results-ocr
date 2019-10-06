#include <string>
#include <sstream>
#include <algorithm>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

#define DEBUG

unsigned int position_offset = 0, name_offset = 0, ping_offset = 0, time_offset = 0, car_offset = 0, best_lap_offset = 0;

struct TableLayout {
    unsigned int left, top, bottom, right;
    unsigned int line_height, row_height;

    unsigned int position_left, position_right;
    unsigned int name_left, name_right;
    unsigned int car_left, car_right;
    unsigned int time_left, time_right;
    unsigned int lap_left;
};

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
Result results[16];

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

// https://stackoverflow.com/a/4119881
bool iequals(const std::string& a, const std::string& b)
{
    return std::equal(a.begin(), a.end(),
                      b.begin(), b.end(),
                      [](char a, char b) {
                          return tolower(a) == tolower(b);
                      });
}

std::string clean_time(std::string &time)
{
    if (time.compare("DNF") == 0 || time.compare("ONF") == 0) {
        return std::string("DNF");
    } else if (time[0] == '+')  {
        return time;
    }
    time.erase(std::remove_if(time.begin(), time.end(), &is_valid_time_digit), time.end());
    if (time.length() >= 7 && time[2] != ':') {
        time.insert(2, ":");
    }
    if (time.length() >= 7 && time[5] != '.') {
        time.insert(5, ".");
    }
    return time;
}

bool process_line(Result *result, tesseract::ResultIterator* ri, TableLayout *layout) 
{
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
    do {
        const char* word = ri->GetUTF8Text(level);
        float conf = ri->Confidence(level);
        int x1, y1, x2, y2;
        ri->BoundingBox(level, &x1, &y1, &x2, &y2);
        std::string token = word;
        if (result->raw_position.empty() && x1 >= layout->position_left && x2 < layout->position_right) {
            result->raw_position = std::string(word);
            result->position = std::atoi(word);
        } else if (result->name.empty() && x1 >= layout->name_left) {
            result->name = token;
        } else if (!result->name.empty() && x2 < layout->name_right) {
            result->name.append(" ");
            result->name.append(token);
        } else if (x1 >= layout->car_left && x2 < layout->car_right) {
            if (!result->car.empty()) {
                result->car.append(" ");
            }
            result->car.append(token);
        } else if (x1 >= layout->time_left && x2 < layout->time_right) {
            if (!result->time.empty()) {
                result->time.append(" ");
            }
            result->time.append(token);
        } else if (result->best_lap.empty() && x1 >= layout->lap_left) {
            result->best_lap = clean_time(token);
        }
        delete[] word;
    } while (ri->Next(level) && !ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE));
    result->time = clean_time(result->time);
    return ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE);
}

/* Rewrite positions by finding the most common offset (e.g. 0 if viewing
 * positions 1-16, 4 if viewing 5-20). This will introduce errors if whole rows
 * are not detected by the OCR engine. */
void clean_positions()
{
    unsigned char offsets[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (int index = 0; index < 16; ++index) {
        Result *res = &results[index];
        int offset = res->position - index;
        if (offset >= 0 && offset < 10) {
            offsets[offset] += 1;
        }
    }
    int majority_offset = std::max_element(offsets, offsets + 10) - offsets;
    for (int index = 0; index < 16; ++index) {
        Result *res = &results[index];
#ifdef DEBUG
        if (res->position != index + majority_offset) {
            printf("Changing position %d to %d at index %d\n", res->position, index + majority_offset, index);
        }
#endif
        res->position = index + majority_offset;
    }
}

bool detect_layout(Pix *image, tesseract::TessBaseAPI *api, TableLayout *layout)
{
    api->SetImage(image);
    api->Recognize(0);
    tesseract::ResultIterator* ri = api->GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
    while (ri != 0) {
        const char* word = ri->GetUTF8Text(level);
        float conf = ri->Confidence(level);
        int x1, y1, x2, y2;
        ri->BoundingBox(level, &x1, &y1, &x2, &y2);
        std::string token = word;
        delete[] word;
        // TODO: Are there localized column captions in Wreckfest?
        if (iequals(token, "POS")) {
            layout->position_left = x1 - 5;
            layout->position_right = x2 + 10;
            layout->left = x1 - 10;
            layout->top = y2 + 10; 
            layout->line_height = y2 - y1;
            layout->row_height = (int)(layout->line_height * 3) - 1;
        } else if (iequals(token, "NAME")) {
            layout->name_left = x1 - 5;
        } else if (iequals(token, "PING")) { // multiplayer only
            layout->name_right = x1 - 10;
        } else if (iequals(token, "CLASS") && layout->name_right == 0) { // fallback for singleplayer
            layout->name_right = x1 - 10;
        } else if (iequals(token, "CAR")) {
            layout->car_left = x1 - 5;
        } else if (iequals(token, "TIME")) {
            layout->car_right = x1 - 10;
            layout->time_left = x1 - 5;
        } else if (iequals(token, "BEST")) {
            layout->time_right = x1 - 10;
            layout->lap_left = x1 - 5;
            layout->right = x2 + 250; // XXX 
            break;
        }
        if (!ri->Next(level)) {
            break;
        }
    }
    // Scan a single pixel column to find the exact row height and line
    // separator distance which is needed to clear out the separator lines.
    if (layout->position_left != 0) {
        l_uint32 pixel;
        l_int32 sep1start = 0;
        bool dark = false;
        for (int y = layout->top; y < layout->bottom; ++y) {
            pixGetPixel(image, layout->position_left - 5, y, &pixel);
            if (pixel < 180 && !dark) {
                printf("%d@%d ", pixel, y);
                if (sep1start == 0) {
                    sep1start = y;
                } else {
                    layout->row_height = y - sep1start;
                    break;
                }
                dark = true;
            } else if (pixel >= 180 && dark) {
                dark = false;
            }
        }
    }
    layout->bottom = layout->row_height * 16 + layout->top;
    printf("Layout: %d,%d,%d,%d Line height=%d Row height=%d\n", layout->left, layout->top, layout->right, layout->bottom, layout->line_height, layout->row_height);
    return (layout->top != 0);
}

void convert(char *filename, tesseract::TessBaseAPI *api)
{
    l_int32 width, height;
    char *outText;
    TableLayout layout;
    pixReadHeader(filename, nullptr, &width, &height, nullptr, nullptr, nullptr); 
    float aspect_ratio = (float)width / height;
    float crop_factor = 3.0;
    // Crop empty spaces for ultrawide resolutions
    if (aspect_ratio > 2.3 && aspect_ratio < 2.5) {
        crop_factor = 2.66;
    }
    // Roughly crop image to results section
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
    if (!detect_layout(mono_image, api, &layout)) {
        fprintf(stderr, "Could not detect layout.\n");
        exit(1);
    }
    // Blank out player logos
    Box *logos = boxCreate(layout.position_right, 0, layout.name_left - layout.position_right, region_height);
    pixSetInRect(mono_image, logos);
    boxDestroy(&logos);
    // Blank out car class (the symbol can't be extracted and the A and B class
    // text is lost in the image optimization due to the color) 
    Box *car_class = boxCreate(layout.name_right, 0, layout.car_left - layout.name_right, region_height);
    pixSetInRect(mono_image, car_class);
    boxDestroy(&car_class);
    // Blank out line separators
    for (int row = 1; row <= 16; ++row) {
        Box *separator = boxCreate(0, layout.top + (layout.row_height * row) - 1, region_width, layout.line_height);
        pixSetInRect(mono_image, separator);
        boxDestroy(&separator);
    }
#ifdef DEBUG
    pixWritePng("preprocessed.png", mono_image, 0);
#endif
    
    api->SetImage(mono_image);
    api->SetRectangle(layout.left, layout.top, layout.right - layout.left, layout.bottom - layout.top);
    api->Recognize(0);
    tesseract::ResultIterator* ri = api->GetIterator();
    if (ri != 0) {
        for (int index = 0; index < 16; ++index) {
            if (!process_line(&results[index], ri, &layout)) {
                break;
            }
        }
    }

    pixDestroy(&mono_image);
    pixDestroy(&remove_bg);
    pixDestroy(&grey_image);
    pixDestroy(&cropped_image);
    pixDestroy(&image);
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
        for (int index = 0; index < 16; ++index) {
            Result *res = &results[index];
            if (!res->name.empty()) {
                printf("%d,%s,%s,%s,%s\n", res->position, res->name.c_str(), res->car.c_str(), res->time.c_str(), res->best_lap.c_str());
            }
        }
    }
    api->End();
    return 0;
}
