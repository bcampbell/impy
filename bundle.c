#include "impy.h"
#include "private.h"

#include <string.h>

typedef struct slot {
    SlotID id;
    im_img* img;
} slot;

struct im_bundle{
    int nframes;
    int nmipmaps;
    int nlayers;
    int nfaces;

    int capacity;
    int length;
    slot* slots;
};

static int slotid_cmp(const SlotID a, const SlotID b);

static const SlotID slotid_ZERO = {0};

static int slotid_cmp(const SlotID a, const SlotID b)
{
    if (a.frame>b.frame) {
        return 1;
    } else if (a.frame<b.frame) {
        return -1;
    }

    if (a.mipmap>b.mipmap) {
        return 1;
    } else if (a.mipmap<b.mipmap) {
        return -1;
    }

    if (a.layer>b.layer) {
        return 1;
    } else if (a.layer<b.layer) {
        return -1;
    }

    if (a.face>b.face) {
        return 1;
    } else if (a.face<b.face) {
        return -1;
    }

    // if we got this far, they're equal!
    return 0;
}


im_bundle* im_bundle_new()
{
    const int initialcap = 8;
    im_bundle* b = imalloc(sizeof(im_bundle));

    if (b==NULL) {
        return NULL;
    }

    b->nframes = 0;
    b->nmipmaps = 0;
    b->nlayers = 0;
    b->nfaces = 0;

    b->length = 0;
    b->slots = imalloc(initialcap*sizeof(slot));
    b->capacity = initialcap;
    if (b->slots==NULL) {
        ifree(b);
        return NULL;
    }

    return b;
}


// delete a bundle and all its images
void im_bundle_free(im_bundle* b)
{
    int i;
    for(i=0; i<b->length; ++i) {
        im_img* img = b->slots[i].img;
        if (img) {
            im_img_free( img );
        }
    }
    ifree(b->slots);
    ifree(b);
}


static bool grow_bundle( im_bundle* b) {
    int newcap = b->capacity*2;
    slot* newmem = imalloc(newcap * sizeof(slot));
    if (newmem==NULL ) {
        return false;
    }
    memcpy( newmem, b->slots, b->length*sizeof(slot));
    ifree(b->slots);
    b->slots = newmem;
    b->capacity = newcap;
    return true;
}

static int find_slot(im_bundle* b, const SlotID id)
{
    // TODO: just a linear search. fine for small sets, but the idea is that
    // we'll keep the list sorted, so we can do a quick binary search instead.
    int i;
    for(i=0; i<b->length; ++i) {
        if(slotid_cmp(id, b->slots[i].id) == 0 ) {
            return i;
        }
    }
    return -1;
}

im_img* im_bundle_get(im_bundle* b, const SlotID id)
{
    int idx = find_slot(b,id);
    if (idx == -1) {
        return NULL;
    }
    return b->slots[idx].img;
}


bool im_bundle_set(im_bundle* b, const SlotID id, im_img* img)
{
    int idx = find_slot(b,id);
    if (idx != -1) {
        // replace existing
        im_img_free(b->slots[idx].img);
        b->slots[idx].img = img;
        return true;
    }

    // it's a new entry

    if (b->length >= b->capacity) {
        if (!grow_bundle(b)) {
            return false;
        }
    }

    // TODO: insert in order of id, so we can binary search
    // but for now... just append
    idx = b->length;
    ++b->length;
    b->slots[idx].id = id;
    b->slots[idx].img = img;

    // update the maximum frame/mip/layer/face values
    if (id.frame+1 > b->nframes ) {
        b->nframes = id.frame+1;
    }
    if (id.mipmap+1 > b->nmipmaps ) {
        b->nmipmaps = id.mipmap+1;
    }
    if (id.layer+1 > b->nlayers ) {
        b->nlayers = id.layer+1;
    }
    if (id.face+1 > b->nfaces ) {
        b->nfaces = id.face+1;
    }

    return true;
}


int im_bundle_num_frames(im_bundle* b)
{
    return b->nframes;
}

im_img* im_bundle_get_frame(im_bundle* b, int n)
{
    SlotID id = {0};
    id.frame = n;
    return im_bundle_get(b,id);
}



