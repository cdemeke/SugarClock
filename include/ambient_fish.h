#ifndef AMBIENT_FISH_H
#define AMBIENT_FISH_H

// Initialize the selected companion's transient animation state.
void ambient_fish_init();

// Draw one frame of the selected companion into the display back buffer.
// The caller owns display_show(); this renderer clears the back buffer first.
void ambient_fish_render();

// Trigger the companion's short, silent greeting to the right button.
void ambient_fish_interact();

#endif // AMBIENT_FISH_H
