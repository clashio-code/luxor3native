// Read-only diagnostic for the C++ gameplay metadata parsers.
// Usage: ./config_audit <game-root>
#include "luxor.h"

#include <cstdio>
#include <dirent.h>
#include <string>

using namespace luxor;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game-root>\n", argv[0]);
        return 2;
    }
    const std::string root = argv[1];
    int maps = 0, loaded = 0, failed = 0;
    DIR* dirs = opendir((root + "/data/maps").c_str());
    if (!dirs) {
        fprintf(stderr, "cannot open %s/data/maps\n", root.c_str());
        return 1;
    }
    while (struct dirent* ent = readdir(dirs)) {
        std::string name = ent->d_name;
        if (name == "." || name == "..") continue;
        const std::string path = root + "/data/maps/" + name + "/pathGamePlayData.txt";
        FILE* file = fopen(path.c_str(), "rb");
        if (!file) continue;
        fclose(file);
        ++maps;
        PathGameplayData data;
        std::string err;
        if (!load_path_gameplay_data(path, &data, &err)) {
            ++failed;
            printf("FAIL %-24s %s\n", name.c_str(), err.c_str());
            continue;
        }
        ++loaded;
        printf("%-24s start=%2zu end=%2zu danger=%2zu occluded=%2zu unshootable=%2zu spawnrestrictive=%2zu underwater=%2zu switches=%2zu unknown=%2zu\n",
               name.c_str(), data.start_speed.size(), data.end_speed.size(),
               data.danger_sections.size(), data.occluded_sections.size(),
               data.unshootable_sections.size(), data.spawn_restrictive_sections.size(),
               data.underwater_sections.size(), data.switches.size(),
               data.unknown_directives.size());
    }
    closedir(dirs);
    printf("map files: %d, loaded: %d, failed: %d\n", maps, loaded, failed);

    const char* configs[] = {"GameVariables.txt", "DifficultyGameVariables.txt"};
    for (const char* name : configs) {
        const std::string path = root + "/data/" + name;
        FILE* file = fopen(path.c_str(), "rb");
        if (!file) continue;  // optional config may not exist in synthetic fixture
        fclose(file);
        NameValueConfig cfg;
        std::string err;
        if (!load_name_value_config(path, &cfg, &err)) {
            printf("FAIL %s: %s\n", name, err.c_str());
            continue;
        }
        printf("%s: keys=%zu includes=%zu", name, cfg.values.size(), cfg.included_files.size());
        auto it = cfg.values.find("ShooterSecondsBeforeNextShot");
        if (it != cfg.values.end()) printf(" ShooterSecondsBeforeNextShot=%s", it->second.c_str());
        it = cfg.values.find("TrainAcceleration");
        if (it != cfg.values.end()) printf(" TrainAcceleration=%s", it->second.c_str());
        printf("\n");
    }
    return failed == 0 ? 0 : 1;
}
