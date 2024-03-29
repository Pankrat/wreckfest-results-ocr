#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <map>
#include <filesystem>
#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#define DEBUG

struct TableLayout {
    unsigned int left, top, right, bottom;

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

std::map<std::string, std::string> drivers; // Driver -> Team
std::map<std::string, int> points; // Position/Label -> Points

const l_uint32 edge_detection_threshold_low = 190;
const l_uint32 edge_detection_threshold_high = 240;

// Source: https://rosettacode.org/wiki/Levenshtein_distance#C.2B.2B
// Compute Levenshtein Distance
// Martin Ettl, 2012-10-05
size_t uiLevenshteinDistance(const std::string &s1, const std::string &s2)
{
    const size_t m(s1.size());
    const size_t n(s2.size());

    if( m==0 ) return n;
    if( n==0 ) return m;

    size_t *costs = new size_t[n + 1];

    for( size_t k=0; k<=n; k++ ) costs[k] = k;

    size_t i = 0;
    for ( std::string::const_iterator it1 = s1.begin(); it1 != s1.end(); ++it1, ++i )
    {
        costs[0] = i+1;
        size_t corner = i;

        size_t j = 0;
        for ( std::string::const_iterator it2 = s2.begin(); it2 != s2.end(); ++it2, ++j )
        {
            size_t upper = costs[j+1];
            if( *it1 == *it2 )
            {
                costs[j+1] = corner;
            }
            else
            {
                size_t t(upper<corner?upper:corner);
                costs[j+1] = (costs[j]<t?costs[j]:t)+1;
            }

            corner = upper;
        }
    }

    size_t result = costs[n];
    delete [] costs;

    return result;
}

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
    return (!(std::isalnum(c) || (c == ' ')));
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

std::string get_output_filename(const char* filename, const char* extension)
{
    std::filesystem::path p = filename;
    p.replace_extension(extension);
    return p.string();
}

std::string clean_driver(std::string &driver)
{
    if (drivers.find(driver) == drivers.end()) {
        size_t min_distance = driver.length() / 2;
        std::string similar_name;
        // Iterate over known drivers, compute levenshtein distance, choose the min distance
        for (auto iter = drivers.begin(); iter != drivers.end(); ++iter) {
            const std::string adriver = iter->first;
            const size_t distance = uiLevenshteinDistance(driver, adriver);
            if (distance < min_distance) {
                min_distance = distance;
                similar_name = adriver;
            }
        }
        if (!similar_name.empty()) {
            printf("Replacing %s with %s (levenshtein distance is %zu)\n", driver.c_str(), similar_name.c_str(), min_distance);
            return similar_name;
        }
    }
    return driver;
}

std::string clean_time(std::string &time)
{
    if (time.compare("DNF") == 0 || time.compare("ONF") == 0) {
        return std::string("DNF");
    } else if (time[0] == '+')  {
        if (time[2] == 'L') {
            time.insert(2, " ");
        }
        return time;
    }
    time.erase(std::remove_if(time.begin(), time.end(), &is_invalid_time_digit), time.end());
    if (time.length() >= 7 && time[2] != ':') {
        time.insert(2, ":");
    }
    if (time.length() >= 7 && time[5] != '.') {
        time.insert(5, ".");
    }
    // Special handling for times where the leading zero is missing in lap
    // times (introduces an error if the missed digit is non-zero)
    if (time.find_first_of(':') == 1) {
        time.insert(0, "0");
    }
    return time;
}

std::string clean_car(std::string &car)
{
    car.erase(std::remove_if(car.begin(), car.end(), &is_invalid_car_digit), car.end());
    return car;
}

void read_drivers(std::map<std::string, std::string> *drivers, const char *filename)
{
    std::ifstream file;
    std::string line;
    file.open(filename);
    if (!file) {
        printf("Can't read drivers from %s.\n", filename);
        return;
    }
    for (std::string line; getline(file, line);) {
        // Split on first ,
        size_t sep_offset = line.find_first_of(',');
        if (sep_offset == std::string::npos) {
            (*drivers)[line] = "";
        } else {
            std::string team = line.substr(0, sep_offset);
            std::string driver = line.substr(sep_offset + 1, line.length() - sep_offset - 1);
            (*drivers)[driver] = team;
        }
    }
    file.close();
}

void read_points(std::map<std::string, int> *points, const char *filename)
{
    std::ifstream file;
    std::string label;
    int score;
    file.open(filename);
    if (!file) {
        printf("Can't read points from %s.\n", filename);
        return;
    }
    while (file >> label >> score) {
        (*points)[label] = score;
    }
    file.close();
}

Result *process_line(tesseract::ResultIterator* ri, TableLayout *layout) 
{
    if (!ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) {
        return nullptr;
    }
    Result *result = new Result;
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
		printf("DEBUG: %s @ %d %d %d %d\n", word, x1, y1, x2, y2);

        if (result->raw_position.empty() && x1 >= (int)layout->position_left && x2 < (int)layout->position_right) {
            result->raw_position = std::string(word);
            result->position = std::atoi(word);
        } else if (result->name.empty() && x1 >= (int)layout->name_left - 15) {
            result->name = token;
        } else if (!result->name.empty() && x2 < (int)layout->name_right) {
            result->name.append(" ");
            result->name.append(token);
        } else if (x1 >= (int)layout->car_left - 15 && x2 < (int)layout->car_right) {
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
    result->name = clean_driver(result->name);
    result->car = clean_car(result->car);
    result->time = clean_time(result->time);
    result->best_lap = clean_time(result->best_lap);
    result->dnf = iequals(result->time, "DNF");
    return result;
}

/* Rewrite positions by finding the most common offset (e.g. 0 if viewing
 * positions 1-16, 4 if viewing 5-20). This will introduce errors if whole rows
 * are not detected by the OCR engine. */
void clean_positions(std::vector<Result> *results)
{
    unsigned char offsets[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    for (unsigned int index = 0; index < results->size(); ++index) {
        Result *res = &(*results)[index];
        int offset = res->position - index;
        if (offset >= 0 && offset < 10) {
            offsets[offset] += 1;
        }
    }
    int majority_offset = std::max_element(offsets, offsets + 10) - offsets;
    for (unsigned int index = 0; index < results->size(); ++index) {
        Result *res = &(*results)[index];
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
    layout->bottom = pixGetHeight(image);
    layout->right = pixGetWidth(image);
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
            layout->name_left = x1;
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
        } else if (iequals(token, "BEST") || iequals(token, "BESTLAP")) {
            layout->time_right = x1 - 15;
            layout->lap_left = x1 - 15;
            layout->right = x2 + 250; // XXX 
            break;
        } else if (iequals(token, "SCORE")) {
            layout->wreck_ratio_right = x1 - 15;
            layout->score_left = x1 - 15;
            layout->right = x2 + 100; // XXX
            break;
        }
        if (!ri->Next(level)) {
            break;
        }
    }
    printf("Layout: %d,%d,%d,%d\n", layout->left, layout->top, layout->right, layout->bottom);
    return (layout->top != 0);
}

void blank_separators(Pix *image, TableLayout *layout)
{
    // Scan a single pixel column to find the exact row height and line
    // separator distance which is needed to clear out the separator lines.
    if (layout->position_left != 0) {
        l_uint32 pixel = 0;
        l_int32 sepstart = -1;
        const l_int32 column = layout->position_left - 5;
        bool dark = false;
        printf("DEBUG: Scanning column %d\n", column);
        for (unsigned int y = layout->top; y < layout->bottom; ++y) {
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

Pix *preprocess(const char *filename, tesseract::TessBaseAPI *api, TableLayout *layout)
{
    l_int32 width, height;
    Pix *image = pixRead(filename);
    if (image == nullptr) {
        fprintf(stderr, "Could not read input file %s.\n", filename);
        exit(1);
    }
    pixGetDimensions(image, &width, &height, nullptr);
    float aspect_ratio = (float)width / height;
    float crop_factor = 3.0;
    // Crop empty spaces for ultrawide resolutions
    if (aspect_ratio > 2.3 && aspect_ratio < 2.5) {
        crop_factor = 2.66;
    }
    // Roughly crop image to results section
    l_int32 left = (l_int32)(width / crop_factor);
    l_int32 top = height / 5;
    l_int32 region_width = width - left;
    l_int32 region_height = height - top - (height / 12);
    Box *box = boxCreate(left, top, region_width, region_height);
    Pix *cropped_image = pixClipRectangle(image, box, nullptr);
    // Optimize image for OCR (negative colors, contrast enhancement, background removal)
    pixInvert(cropped_image, cropped_image);
    Pix *grey_image = pixConvertRGBToLuminance(cropped_image);
    pixContrastTRC(grey_image, grey_image, 0.6);
    Pix *remove_bg = pixBackgroundNormSimple(grey_image, NULL, NULL);
    Pix *mono_image = pixCleanBackgroundToWhite(remove_bg, NULL, NULL, 1.0, 50, 190);
    if (!detect_layout(mono_image, api, layout)) {
        fprintf(stderr, "Could not detect layout.\n");
        exit(1);
    }
    // Blank out player logos
    Box *logos = boxCreate(layout->position_right, 0, layout->name_left - layout->position_right, region_height);
    pixSetInRect(mono_image, logos);
    boxDestroy(&logos);
    // Blank out car class (the symbol can't be extracted and the A and B class
    // text is lost in the image optimization due to the color) 
    Box *car_class = boxCreate(layout->name_right, 0, layout->car_left - layout->name_right, region_height);
    pixSetInRect(mono_image, car_class);
    boxDestroy(&car_class);
    // Blank out line separators
    blank_separators(mono_image, layout);
#ifdef DEBUG
    pixWritePng(get_output_filename(filename, ".preprocessed.png").c_str(), mono_image, 0);
#endif
    pixDestroy(&remove_bg);
    pixDestroy(&grey_image);
    pixDestroy(&cropped_image);
    pixDestroy(&image);
    boxDestroy(&box);
    return mono_image;
}

std::vector<Result> convert(const char *filename, tesseract::TessBaseAPI *api)
{
    std::vector<Result> results{};
    Result *result;
    TableLayout layout{};
    Pix *image = preprocess(filename, api, &layout);
    api->SetImage(image);
    api->SetRectangle(layout.left, layout.top, layout.right - layout.left, layout.bottom - layout.top);
    api->Recognize(0);
    tesseract::ResultIterator* ri = api->GetIterator();
    if (ri != 0) {
        while ((result = process_line(ri, &layout)) != nullptr) {
            results.push_back(*result);
        }
    }
    pixDestroy(&image);
    return results;
}

int get_points(const Result *result)
{
    // TODO: Find fastest lap across all results
    if (result->dnf) {
        return points["DNF"];
    } else {
        return points[std::to_string(result->position)];
    }
}

std::map<std::string, int> get_team_results(std::vector<Result> results)
{
    std::map<std::string, int> team_results;
    for (auto &res: results) {
        int points = get_points(&res);
        std::string team = drivers[res.name];
        if (team_results.find(team) == team_results.end()) {
            team_results[team] = points;
        } else {
            team_results[team] += points;
        }
    }
    return team_results;
}

void write_results(const std::string &filename, const std::vector<Result> results)
{
    std::ofstream csvfile;
    csvfile.open(filename, std::ios::trunc);
    if (results[0].derby) {
        csvfile << "Position,Name,Car,Wreck Ratio,Score\n";
    } else {
        csvfile << "Position,Name,Car,Time,Best Lap\n";
    }
    for (auto &res: results) {
        if (!res.name.empty()) {
            csvfile << res.position;
            csvfile << ',' << res.name << ',' << res.car << ',';
            if (res.derby) {
                csvfile << res.wreck_ratio << ',' << res.score << std::endl;
            } else {
                csvfile << res.time << ',' << res.best_lap << std::endl;
            }
        }
    }
    csvfile.close();
}

void write_annotated_results(const std::string &filename, const std::vector<Result> results)
{
    std::ofstream csvfile;
    csvfile.open(filename, std::ios::trunc);
    if (results[0].derby) {
        csvfile << "Position,Name,Team,Car,Wreck Ratio,Score,Points\n";
    } else {
        csvfile << "Position,Name,Team,Car,Time,Best Lap,Points\n";
    }
    for (auto &res: results) {
        std::string team = drivers[res.name];
        if (!res.name.empty()) {
            csvfile << res.position;
            csvfile << ',' << res.name << ',' << team << ',' << res.car << ',';
            if (res.derby) {
                csvfile << res.wreck_ratio << ',' << res.score << ',';
            } else {
                csvfile << res.time << ',' << res.best_lap << ',';
            }
            csvfile << get_points(&res) << std::endl;
        }
    }
    csvfile.close();
}

void write_team_results(const std::string &filename, std::map<std::string, int> team_results)
{
    std::ofstream csvfile;
    csvfile.open(filename, std::ios::trunc);
    csvfile << "Team,Points\n";
    for (auto it = team_results.begin(); it != team_results.end(); ++it) {
        csvfile << it->first << ',' << it->second << std::endl;
    }
    csvfile.close();
}

int main(int argc, char *argv [])
{
    tesseract::TessBaseAPI *api = new tesseract::TessBaseAPI();
    if (api->Init(NULL, "eng")) {
        fprintf(stderr, "Could not initialize tesseract.\n");
        exit(1);
    }
    read_drivers(&drivers, "drivers.txt");
    read_points(&points, "points.txt");
    for (int i = 1; i < argc; ++i) {
        char *filename = argv[i];
#ifdef DEBUG
        printf("Processing %d / %d %s ...\n", i, argc-1, filename);
#endif
        std::vector<Result> results{};
        results = convert(filename, api);
        clean_positions(&results);
        std::string csvname = get_output_filename(filename, ".csv");
        write_results(csvname, results);
        csvname = get_output_filename(filename, ".annotated.csv");
        write_annotated_results(csvname, results);
        csvname = get_output_filename(filename, ".team.csv");
        write_team_results(csvname, get_team_results(results));
    }
    api->End();
    return 0;
}
