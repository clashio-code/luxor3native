// Diagnostic: how many of the real levels can the current C++ loader
// actually drive? Reads the shipped data tree; prints only.
#include "luxor.h"

#include <cstdio>
#include <dirent.h>
#include <string>
#include <vector>

using namespace luxor;

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <game-root>\n", argv[0]);
        return 2;
    }
    std::string root = argv[1];
    PathTable paths;
    paths.scan(root);

    // Collect every level file through the filesystem.
    std::vector<std::string> levels;
    for (const char* dir : {"classic", "puzzle", "survival", "crossingTheNile",
                             "treasureHunter", "centipede"}) {
        std::string base = root + "/data/levels/" + dir;
        DIR* handle = opendir(base.c_str());
        if (!handle) continue;
        while (struct dirent* entry = readdir(handle)) {
            std::string name = entry->d_name;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".lvl") == 0)
                levels.push_back(base + "/" + name);
        }
        closedir(handle);
    }

    int total = 0, with_spawn = 0, with_map = 0, map_ok = 0, section_ok = 0;
    for (const auto& path : levels) {
        ++total;
        Level lv;
        std::string err;
        if (!load_level(path, &lv, &err)) continue;
        if (!lv.map_path.empty()) ++with_map;
        if (!lv.spawn_order.empty()) {
            ++with_spawn;
            Track track;
            std::string terr;
            bool map_found = load_track(root + "/" + lv.map_path + "path.LX3TRK",
                                        &track, &terr);
            if (map_found) {
                ++map_ok;
                bool all = true;
                for (const auto& s : lv.spawn_order)
                    if (!track.find_section(s)) all = false;
                if (all) ++section_ok;
            }
        }
    }
    printf("levels found          : %d\n", total);
    printf("map path parsed       : %d\n", with_map);
    printf("spawn order parsed    : %d\n", with_spawn);
    printf("  + track file loads  : %d\n", map_ok);
    printf("  + all spawn sections resolve: %d\n", section_ok);

    printf("\nper mode:\n");
    for (const char* dir : {"classic", "puzzle", "survival", "crossingTheNile",
                             "treasureHunter", "centipede"}) {
        int count = 0, spawn = 0, trains = 0, spheres = 0, flavors = 0;
        std::string base = root + "/data/levels/" + dir;
        DIR* handle = opendir(base.c_str());
        if (!handle) continue;
        while (struct dirent* entry = readdir(handle)) {
            std::string name = entry->d_name;
            if (name.size() <= 4 || name.compare(name.size() - 4, 4, ".lvl") != 0)
                continue;
            ++count;
            Level lv;
            std::string err;
            if (!load_level(base + "/" + name, &lv, &err)) continue;
            if (!lv.spawn_order.empty()) ++spawn;
            if (lv.num_trains > 0) ++trains;
            if (lv.spheres_per_train > 0) ++spheres;
            if (!lv.flavors.empty()) ++flavors;
        }
        closedir(handle);
        printf("  %-16s levels=%3d spawnOrder=%3d numTrains=%3d spheres=%3d flavors=%3d\n",
               dir, count, spawn, trains, spheres, flavors);
    }
    return 0;
}
