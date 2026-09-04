#ifndef AMBIENT_GHOST_H
#define AMBIENT_GHOST_H

// Initialize the ambient ghost's transient animation state.
void ambient_ghost_init();

// Draw one frame of the ambient ghost into the display back buffer.
// The caller owns display_show(); this renderer clears the back buffer first.
void ambient_ghost_render();

// Trigger the ghost's short, silent wave response to the right button.
void ambient_ghost_interact();

#endif // AMBIENT_GHOST_H
