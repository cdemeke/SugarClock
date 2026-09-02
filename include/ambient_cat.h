#ifndef AMBIENT_CAT_H
#define AMBIENT_CAT_H

// Initialize the ambient cat's transient animation state.
void ambient_cat_init();

// Draw one frame of the ambient cat into the display back buffer.
// The caller owns display_clear() / display_show().
void ambient_cat_render();

// Trigger the short, silent acknowledgement animation used by the right button.
void ambient_cat_interact();

#endif // AMBIENT_CAT_H
