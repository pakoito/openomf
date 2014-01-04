#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <dumb/dumb.h>
#include <enet/enet.h>

#include "engine.h"
#include "shadowdive/stringparser.h"
#include "utils/log.h"
#include "utils/random.h"
#include "game/game_state.h"
#include "game/settings.h"

int main(int argc, char *argv[]) {
    // Get path

    char *path = "";
    char *ip = NULL;
    int net_mode = NET_MODE_NONE;


#ifndef DEBUGMODE
    path = SDL_GetPrefPath("AnanasGroup", "OpenOMF");
    if(path == NULL) {
        printf("Error: %s\n", SDL_GetError());
        return 1;
    }
    printf("Config path is %s\n", path);
    SDL_free(path);

    path = SDL_GetBasePath();
    if(path == NULL) {
        printf("Error: %s\n", SDL_GetError());
        return 1;
    }
    printf("Base path is %s\n", path);
    SDL_free(path);
#endif

    // Config path
    char config_path[strlen(path)+32];
    sprintf(config_path, "%s%s", path, "openomf.conf");

    // Logfile path
    char logfile_path[strlen(path)+32];
    sprintf(logfile_path, "%s%s", path, "openomf.log");

    // Check arguments
    if(argc >= 2) {
        if(strcmp(argv[1], "-v") == 0) {
            printf("OpenOMF v0.1\n");
            printf("Source available at https://github.com/omf2097/ (MIT License)\n");
            printf("(C) 2013 Tuomas Virtanen\n");
            return 0;
        } else if(strcmp(argv[1], "-h") == 0) {
            printf("Arguments:\n");
            printf("-h       Prints this help\n");
            printf("-w       Writes a config file\n");
            printf("-c <ip>  Connect to server\n");
            printf("-l       Start server\n");
            return 0;
        } else if(strcmp(argv[1], "-w") == 0) {
            if(settings_write_defaults(config_path)) {
                fprintf(stderr, "Failed to write config file to '%s'!\n", config_path);
                fflush(stderr);
                return 1;
            } else {
                printf("Config file written to '%s'!\n", config_path);
            }
            return 0;
        } else if(strcmp(argv[1], "-c") == 0) {
            if(argc >= 3) {
                ip = strcpy(malloc(strlen(argv[2])+1), argv[2]);
            }
            net_mode = NET_MODE_CLIENT;
        } else if(strcmp(argv[1], "-l") == 0) {
            net_mode = NET_MODE_SERVER;
        }
    }

    // Init log
#if defined(DEBUGMODE) || defined(STANDALONE_SERVER)
    if(log_init(0)) {
        fprintf(stderr, "Error while initializing log!\n");
        fflush(stderr);
        return 1;
    }
#else
    if(log_init(logfile_path)) {
        fprintf(stderr, "Error while initializing log '%s'!\n", logfile_path);
        fflush(stderr);
        return 1;
    }
#endif

    // Random seed
    rand_seed(time(NULL));

    // Init stringparser
    sd_stringparser_lib_init();

    // Init config
    if(settings_init(config_path)) {
        goto exit_0;
    }
    settings_load();

    if(ip) {
        settings_get()->net.net_server_ip = ip;
    }

    // Init SDL2
#ifdef STANDALONE_SERVER
    if(SDL_Init(SDL_INIT_TIMER)) {
#else
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)) {
#endif
        PERROR("SDL2 Initialization failed: %s", SDL_GetError());
        goto exit_1;
    }
    SDL_version sdl_linked;
    SDL_GetVersion(&sdl_linked);
    INFO("Found SDL v%d.%d.%d", sdl_linked.major, sdl_linked.minor, sdl_linked.patch);
    INFO("Running on platform: %s", SDL_GetPlatform());

#ifndef STANDALONE_SERVER
    if(SDL_InitSubSystem(SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER|SDL_INIT_HAPTIC)) {
        PERROR("SDL2 Initialization failed: %s", SDL_GetError());
        goto exit_1;
    }
    INFO("Found %d joysticks attached", SDL_NumJoysticks());
    SDL_Joystick *joy;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        joy = SDL_JoystickOpen(0);
        if (joy) {
            INFO("Opened Joystick %d", i);
            INFO(" * Name:              %s", SDL_JoystickNameForIndex(i));
            INFO(" * Number of Axes:    %d", SDL_JoystickNumAxes(joy));
            INFO(" * Number of Buttons: %d", SDL_JoystickNumButtons(joy));
            INFO(" * Number of Balls:   %d", SDL_JoystickNumBalls(joy));
            INFO(" * Number of Hats:    %d", SDL_JoystickNumHats(joy));
        } else {
            INFO("Joystick %d is unsupported", i);
        }

        if (SDL_JoystickGetAttached(joy)) {
            SDL_JoystickClose(joy);
        }
    }

    // Init libDumb
    dumb_register_stdfiles();
#endif

    // Init enet
    if(enet_initialize() != 0) {
        PERROR("Failed to initialize enet");
        goto exit_2;
    }
    
    // Initialize engine
    if(engine_init()) {
        PERROR("Failed to initialize game engine.");
        goto exit_3;
    }
    
    // Run
    engine_run(net_mode);
    
    // Close everything
    engine_close();
exit_3:
    enet_deinitialize();
exit_2:
    SDL_Quit();
exit_1:
    dumb_exit();
    settings_save();
    settings_free();
exit_0:
    sd_stringparser_lib_deinit();
    INFO("Exit.");
    log_close();
#ifndef DEBUGMODE
    /*
    SDL_free(path);
    */
#endif
    return 0;
}
