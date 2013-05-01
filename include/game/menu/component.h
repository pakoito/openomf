#ifndef _COMPONENT_H
#define _COMPONENT_H

typedef struct component_t component;

/*
* This is the basic component that you get by creating any textbutton, togglebutton, etc.
* The point is to abstract away rendering and event handling
*/
struct component_t {
    int x,y,w,h;
    void *obj;
    void (*render)(component *c);
    void (*event)(component *c);
    void (*layout)(component *c, int x, int y, int w, int h);
};

void component_create(component *c);
void component_free(component *c);
void component_layout(component *c, int x, int y, int w, int h);

#endif // _COMPONENT_H