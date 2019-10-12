#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <experimental/filesystem>
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#define DEBUG

struct TableLayout {
    unsigned int left, top, right;

    unsigned int position_left, position_right;
    unsigned int name_left, name_right;
    unsigned int car_left, car_right;
    unsigned int time_left, time_right;
    unsigned int lap_left;
    unsigned int wreck_ratio_left, wreck_ratio_right;
    unsigned int score_left;
};

struct Result {
    unsigned short int position;
    std::string raw_position;
    std::string name;
    std::string car;
    std::string time; // race only
    std::string best_lap; // race only
    std::string wreck_ratio; // derby only
    std::string score; // derby only
    bool dnf;
    bool derby;
};

namespace fs = std::experimental::filesystem;

// TODO: allocate dynamically instead of using global variables
Result results[16];

const l_uint32 edge_detection_threshold_low = 190;
const l_uint32 edge_detection_threshold_high = 240;

bool is_invalid_time_digit(char c)
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

bool is_invalid_car_digit(char c)
{
    return not (std::isalnum(c) || (c == ' '));
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
    time.erase(std::remove_if(time.begin(), time.end(), &is_invalid_time_digit), time.end());
    if (time.length() >= 7 && time[2] != ':') {
        time.insert(2, ":");
    }
    if (time.length() >= 7 && time[5] != '.') {
        time.insert(5, ".");
    }
    return time;
}

std::string clean_car(std::string &car)
{
    car.erase(std::remove_if(car.begin(), car.end(), &is_invalid_car_digit), car.end());
    return car;
}

bool process_line(Result *result, tesseract::ResultIterator* ri, TableLayout *layout) 
{
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
    do {
        const char* word = ri->GetUTF8Text(level);
        int x1, y1, x2, y2;
        ri->BoundingBox(level, &x1, &y1, &x2, &y2);

        if (word == nullptr) {
            printf("ERROR: GetUTF8Text returned NULL on %d %d %d %d\n", x1, y1, x2, y2);
            break;
        }

        std::string token = word;
        if (result->raw_position.empty() && x1 >= (int)layout->position_left && x2 < (int)layout->position_right) {
            result->raw_position = std::string(word);
            result->position = std::atoi(word);
        } else if (result->name.empty() && x1 >= (int)layout->name_left) {
            result->name = token;
        } else if (!result->name.empty() && x2 < (int)layout->name_right) {
            result->name.append(" ");
            result->name.append(token);
        } else if (x1 >= (int)layout->car_left && x2 < (int)layout->car_right) {
            if (!result->car.empty()) {
                result->car.append(" ");
            }
            result->car.append(token);
        } else if (x1 >= (int)layout->time_left && x2 < (int)layout->time_right) {
            if (!result->time.empty()) {
                result->time.append(" ");
            }
            result->time.append(token);
            result->derby = false;
        } else if (x1 >= (int)layout->wreck_ratio_left && x2 < (int)layout->wreck_ratio_right) {
            if (!result->wreck_ratio.empty()) {
                result->wreck_ratio.append(" ");
            }
            result->wreck_ratio.append(token);
            result->derby = true;
        } else if (x1 >= (int)layout->lap_left && !result->derby) {
            if (!result->best_lap.empty()) {
                result->best_lap.append(" ");
            }
            result->best_lap.append(token);
        } else if (x1 >= (int)layout->score_left && result->derby) {
            result->score = token;
        }
        delete[] word;
    } while (ri->Next(level) && !ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE));
    result->car = clean_car(result->car);
    result->time = clean_time(result->time);
    result->best_lap = clean_time(result->best_lap);
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
    ri->SetBoundingBoxComponents(false, false);
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
    while (ri != 0) {
        const char* word = ri->GetUTF8Text(level);
        int x1, y1, x2, y2;
        ri->BoundingBox(level, 0, &x1, &y1, &x2, &y2);
        float height, ascenders, descenders;
        ri->RowAttributes(&height, &descenders, &ascenders);
        std::string token = word;
        printf("%s @ %d %d %d %d [%f %f %f]\n", word, x1, y1, x2, y2, height, ascenders, descenders);
        delete[] word;
        // TODO: Are there localized column captions in Wreckfest?
        if (iequals(token, "POS")) {
            layout->position_left = x1 - 5;
            layout->position_right = x2 + 10;
            layout->left = x1;
            layout->top = y2; 
        } else if (iequals(token, "NAME")) {
            layout->name_left = x1 - 5;
        } else if (iequals(token, "PING")) { // multiplayer only
            layout->name_right = x1 - 10;
        } else if (iequals(token, "CLASS") && layout->name_right == 0) { // fallback for singleplayer
            layout->name_right = x1 - 10;
        } else if (iequals(token, "CAR")) {
            layout->car_left = x1 - 10;
        } else if (iequals(token, "TIME")) {
            layout->car_right = x1 - 10;
            layout->time_left = x1 - 5;
        } else if (iequals(token, "WRECK")) {
            layout->car_right = x1 - 10;
            layout->wreck_ratio_left = x1 - 5;
        } else if (iequals(token, "BEST")) {
            layout->time_right = x1 - 15;
            layout->lap_left = x1 - 15;
            layout->right = x2 + 250; // XXX 
            break;
        } else if (iequals(token, "SCORE")) {
            layout->wreck_ratio_right = x1 - 10;
            layout->score_left = x1 - 5;
            layout->right = x2 + 250; // XXX
            break;
        }
        if (!ri->Next(level)) {
            break;
        }
    }
    printf("Layout: %d,%d,%d\n", layout->left, layout->top, layout->right);
    return (layout->top != 0);
}

void blank_separators(Pix *image, TableLayout *layout)
{
    // Scan a single pixel column to find the exact row height and line
    // separator distance which is needed to clear out the separator lines.
    if (layout->position_left != 0) {
        l_uint32 pixel = 0;
        l_int32 sepstart = -1;
        l_int32 column = layout->position_left - 5;
        bool dark = false;
        printf("DEBUG: Scanning column %d\n", column);
        for (unsigned int y = layout->top; (int)y < pixGetHeight(image); ++y) {
            pixGetPixel(image, column, y, &pixel);
            if (pixel < edge_detection_threshold_low && !dark) {
                printf("%d@%d ", pixel, y);
                if (sepstart == -1) {
                    sepstart = y;
                }
                dark = true;
            } else if (pixel >= edge_detection_threshold_high && dark) {
                printf("-> %d@%d\n", pixel, y);
                Box *separator = boxCreate(0, sepstart - 5, layout->right, y - sepstart + 10);
                pixSetInRect(image, separator);
                boxDestroy(&separator);
                dark = false;
                sepstart = -1;
            }
        }
    }
}

void convert(char *filename, tesseract::TessBaseAPI *api)
{
    l_int32 width, height;
    TableLayout layout{};
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
    Pix *mono_image = pixCleanBackgroundToWhite(remove_bg, NULL, NULL, 1.0, 50, 190);
    if (!detect_layout(mono_image, api, &layout)) {
        fprintf(stderr, "Could not detect layout.\n");
        exit(1);
    }
    // Blank out player logos
    Box *logos = boxCreate(layout.position_right, 0, layout.name_left - layout.position_right + 3, region_height);
    pixSetInRect(mono_image, logos);
    boxDestroy(&logos);
    // Blank out car class (the symbol can't be extracted and the A and B class
    // text is lost in the image optimization due to the color) 
    Box *car_class = boxCreate(layout.name_right, 0, layout.car_left - layout.name_right, region_height);
    pixSetInRect(mono_image, car_class);
    boxDestroy(&car_class);
    // Blank out line separators
    blank_separators(mono_image, &layout);
#ifdef DEBUG
    pixWritePng("preprocessed.png", mono_image, 0);
#endif
    
    api->SetImage(mono_image);
    api->SetRectangle(layout.left, layout.top, layout.right - layout.left, region_height - layout.top);
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

std::string get_output_filename(const char *filename)
{
    fs::path p = filename;
    p.replace_extension(".csv");
    return p.string();
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
        std::ofstream csvfile;
        std::string csvname = get_output_filename(filename);
        csvfile.open(csvname, std::ios::trunc);
        if (results[0].derby) {
            csvfile << "Position,Name,Car,Wreck Ratio,Score\n";
        } else {
            csvfile << "Position,Name,Car,Time,Best Lap\n";
        }
        for (int index = 0; index < 16; ++index) {
            Result *res = &results[index];
            if (!res->name.empty()) {
                csvfile << res->position;
                csvfile << ',' << res->name << ',' << res->car << ',';
                if (res->derby) {
                    csvfile << res->wreck_ratio << ',' << res->score << std::endl;
                } else {
                    csvfile << res->time << ',' << res->best_lap << std::endl;
                }
            }
        }
        csvfile.close();
    }
    api->End();
    return 0;
}
