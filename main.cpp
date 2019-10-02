#include <string>
#include <sstream>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

unsigned int position_offset = 0, name_offset = 0, time_offset = 0, car_offset = 0, best_lap_offset = 0;

struct Result {
    unsigned char position;
    std::string raw_position;
    std::string name;
    std::string time;
    std::string car;
    std::string best_lap;
    bool dnf;
};

Result results[25];

void print_alternative_characters(tesseract::TessBaseAPI *api)
{
    api->SetVariable("lstm_choice_mode", "2");
    api->Recognize(0);
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;
    tesseract::ResultIterator* res_it = api->GetIterator();
    // Get confidence level for alternative symbol choices. Code is based on 
    // https://github.com/tesseract-ocr/tesseract/blob/master/src/api/hocrrenderer.cpp#L325-L344
    std::vector<std::vector<std::pair<const char*, float>>>* choiceMap = nullptr;
    if (res_it != 0) {
        do {
            const char* word;
            float conf;
            int x1, y1, x2, y2, tcnt = 1, gcnt = 1, wcnt = 0;
            res_it->BoundingBox(level, &x1, &y1, &x2, &y2);
            choiceMap = res_it->GetBestLSTMSymbolChoices();
            
            for (auto timestep : *choiceMap) {
                if (timestep.size() > 0) {
                    for (auto & j : timestep) {
                        conf = int(j.second * 100);
                        word =  j.first;
                        printf("%d  symbol: '%s';  \tconf: %.2f; BoundingBox: %d,%d,%d,%d;\n",
                                wcnt, word, conf, x1, y1, x2, y2);
                        gcnt++;
                    }
                    tcnt++;
                }
                wcnt++;
                printf("\n");
            }
        } while (res_it->Next(level));
    }
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
        } else if (result->car.empty() && word_index >= 1 && x1 >= car_offset - 10) {
            result->car = token; // TODO: don't split "EL MATADOR" or merge words
        } else if (result->time.empty() && word_index >= 1 && x1 >= time_offset - 10) {
            result->time = token;
        } else if (result->best_lap.empty() && word_index >= 1 && x1 >= best_lap_offset - 10) {
            result->best_lap = token;
        }
        delete[] word;
        ++word_index;
    } while (ri->Next(level) && !ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE));
    printf("Position %d (%s) [Offset=%d]: NAME=%s CAR=%s TIME=%s LAP=%s\n", result->position, result->raw_position.c_str(), result->position - index, result->name.c_str(), result->car.c_str(), result->time.c_str(), result->best_lap.c_str());
    return ri->IsAtBeginningOf(tesseract::RIL_TEXTLINE);
}

void convert(char *filename, tesseract::TessBaseAPI *api)
{
    l_int32 width, height;
    char *outText;
    int index = 0;
    pixReadHeader(filename, nullptr, &width, &height, nullptr, nullptr, nullptr); 
    // Crop image to results section
    l_int32 left = width / 3;
    l_int32 top = height / 5;
    l_int32 region_width = width - left;
    l_int32 region_height = height - top;
    Box *box = boxCreate(left, top, region_width, region_height);
    Pix *image = pixRead(filename);
    Pix *cropped_image = pixClipRectangle(image, box, nullptr);
    // Optimize image for OCR (negative colors, contrast enhancement, background removal)
    pixInvert(cropped_image, cropped_image);
    Pix *grey_image = pixConvertRGBToLuminance(cropped_image);
    pixContrastTRC(grey_image, grey_image, 0.6);
    Pix *remove_bg = pixBackgroundNormSimple(grey_image, NULL, NULL);
    // Cut out player logos because it confuses the recognition of names
    Box *logos = boxCreate(80, 0, 40, region_height-1); // TODO: specific to FHD
    pixSetInRect(remove_bg, logos);
    Pix *scaled_image = pixScaleToSize(remove_bg, 0, 1200);
    Pix *mono_image = pixCleanBackgroundToWhite(scaled_image, NULL, NULL, 1.0, 70, 190);
    pixWritePng("preprocessed.png", mono_image, 0);
    
    api->SetImage(mono_image);
    api->Recognize(0);
    tesseract::ResultIterator* ri = api->GetIterator();
    if (ri != 0) {
        while (process_line(index, ri)) {
            ++index;
        }
    }
    outText = api->GetUTF8Text();
    printf("OCR output of image %d x %d [%d, %d, %d, %d]:\n%s", width, height, left, top, region_width, region_height, outText);
    delete [] outText;

    pixDestroy(&mono_image);
    pixDestroy(&scaled_image);
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
        printf("Processing %d / %d %s ...\n", i, argc-1, filename);
        convert(filename, api);
    }
    api->End();
    return 0;
}
