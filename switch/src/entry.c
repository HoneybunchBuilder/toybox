#include <SDL2/SDL_log.h>
#include <stdint.h>
#include <switch.h>

extern int32_t SDL_main(int32_t argc, char *argv[]);

int32_t main(int32_t argc, char *argv[]) {
  socketInitializeDefault(); // Initialize sockets
  nxlinkStdio(); // Redirect stdout and stderr over the network to nxlink

  SDL_Log("%s", "Switch Entry Point Reached");

  int32_t ret = SDL_main(argc, argv);

  SDL_Log("SDL_main exited with code %d", ret);

  socketExit(); // Cleanup
  return ret;
}