#include <inttypes.h>
#include <stdlib.h>
#include <shadowdive/stringparser.h>

#include "game/game_state.h"
#include "game/game_player.h"
#include "game/utils/settings.h"
#include "video/video.h"
#include "audio/sink.h"
#include "audio/sound.h"
#include "audio/music.h"
#include "resources/ids.h"
#include "game/protos/player.h"
#include "game/protos/object.h"
#include "utils/str.h"
#include "utils/miscmath.h"
#include "utils/log.h"
#include "utils/random.h"

// ---------------- Private functions ----------------

int isset(sd_stringparser_frame *frame, const char *tag) {
    const sd_stringparser_tag_value *v;
    sd_stringparser_get_tag(frame->parser, frame->id, tag, &v);
    return v->is_set;
}

int get(sd_stringparser_frame *frame, const char *tag) {
    const sd_stringparser_tag_value *v;
    sd_stringparser_get_tag(frame->parser, frame->id, tag, &v);
    return v->value;
}

void player_clear_frame(object *obj) {
    player_sprite_state *s = &obj->sprite_state;
    s->blendmode = BLEND_ALPHA;
    s->flipmode = FLIP_NONE;
    s->method_flags = 0;
    s->timer = 0;
    s->duration = 0;

    s->disable_gravity = 0;

    s->screen_shake_horizontal = 0;
    s->screen_shake_vertical = 0;

    s->blend_start = 0xFF;
    s->blend_finish = 0xFF;

    s->pal_begin = 0;
    s->pal_end = 0;
    s->pal_ref_index = 0;
    s->pal_start_index = 0;
    s->pal_entry_count = 0;
    s->pal_tint = 0;
}

int next_frame_with_tag(sd_stringparser *parser, int current_frame, const char *tag, sd_stringparser_frame *f) {
    int frames = sd_stringparser_num_frames(parser);
    int res = 0;
    for(int i = current_frame + 1; i < frames; i++) {
        sd_stringparser_peek(parser, i, f);
        if(isset(f, tag)) {
            return res;
        }
        res += f->duration;
    }
    return -1;
}

int next_frame_with_sprite(sd_stringparser *parser, int current_frame, int sprite, sd_stringparser_frame *f) {
    int frames = sd_stringparser_num_frames(parser);
    int res = 0;
    for(int i = current_frame + 1; i < frames; i++) {
        sd_stringparser_peek(parser, i, f);
        if (f->letter == sprite + 'A') {
            return res;
        }
        res += f->duration;
    }
    return -1;
}


// ---------------- Public functions ----------------

void player_create(object *obj) {
    obj->animation_state.reverse = 0;
    obj->animation_state.end_frame = UINT32_MAX;
    obj->animation_state.ticks = 1;
    obj->animation_state.finished = 0;
    obj->animation_state.entered_frame = 0;
    obj->animation_state.repeat = 0;
    obj->animation_state.spawn = NULL;
    obj->animation_state.spawn_userdata = NULL;
    obj->animation_state.destroy = NULL;
    obj->animation_state.destroy_userdata = NULL;
    obj->animation_state.parser = NULL;
    obj->animation_state.previous = -1;
    obj->animation_state.ticks_len = 0;
    obj->animation_state.parser = sd_stringparser_create();
    obj->animation_state.disable_d = 0;
    obj->animation_state.enemy = NULL;
    obj->slide_state.timer = 0;
    obj->slide_state.vel = vec2f_create(0,0);
    player_clear_frame(obj);
}

void player_free(object *obj) {
    if(obj->animation_state.parser != NULL) {
        sd_stringparser_delete(obj->animation_state.parser);
    }
}

void player_reload_with_str(object *obj, const char* custom_str) {
    // Load new animation string
    sd_stringparser_set_string(
        obj->animation_state.parser,
        custom_str);

    // Find string length
    sd_stringparser_frame tmp;
    obj->animation_state.ticks_len = 0;
    int frames = sd_stringparser_num_frames(obj->animation_state.parser);
    for(int i = 0; i < frames; i++) {
        sd_stringparser_peek(obj->animation_state.parser, i, &tmp);
        obj->animation_state.ticks_len += tmp.duration;
    }

    // Peek parameters
    sd_stringparser_frame param;
    sd_stringparser_peek(obj->animation_state.parser, 0, &param);

    // Set player state
    obj->animation_state.ticks = 1;
    obj->animation_state.finished = 0;
    obj->animation_state.previous = -1;
    obj->animation_state.reverse = 0;

    obj->slide_state.timer = 0;
    obj->slide_state.vel = vec2f_create(0,0);

    obj->enemy_slide_state.timer = 0;
    obj->enemy_slide_state.dest = vec2i_create(0,0);
    obj->enemy_slide_state.duration = 0;

    obj->hit_frames = 0;
    obj->can_hit = 0;
}

void player_reload(object *obj) {
    player_reload_with_str(obj, str_c(&obj->cur_animation->animation_string));
}

void player_reset(object *obj) {
    obj->animation_state.ticks = 1;
    obj->animation_state.finished = 0;
    obj->animation_state.previous = -1;
    sd_stringparser_reset(obj->animation_state.parser);
}

int player_frame_isset(object *obj, const char *tag) {
    sd_stringparser_frame f = obj->animation_state.parser->current_frame;
    return isset(&f, tag);
}

int player_frame_get(object *obj, const char *tag) {
    sd_stringparser_frame f = obj->animation_state.parser->current_frame;
    return get(&f, tag);
}

void player_set_delay(object *obj, int delay) {
    //try to spread <delay> ticks over the 'startup' frames; those that don't spawn projectiles or have hit coordinates
    int r;
    sd_stringparser_frame n;
    int frames = 99;
    // find the first frame that spawns a projectile, if any
    if((r =next_frame_with_tag(obj->animation_state.parser, 0, "m", &n)) >= 0) {
        frames = n.id;
    }

    // find the first frame with hit coordinates
    iterator it;
    collision_coord *cc;
    vector_iter_begin(&obj->cur_animation->collision_coords, &it);
    while((cc = iter_next(&it)) != NULL) {
        if((r = next_frame_with_sprite(obj->animation_state.parser, 0, cc->frame_index, &n)) >= 0) {
            if (n.id < frames) {
                frames = n.id;
            }
        }
    }

    if (!frames) {
        return;
    }

    DEBUG("animation has %d initializer frames", frames);

    int delay_per_frame = delay/frames;
    int rem = delay % frames;
    for(int i = 0; i < frames; i++) {
        int olddur;
        sd_stringparser_peek(obj->animation_state.parser, i, &n);
        olddur = n.duration;
        int newduration = n.duration + delay_per_frame;
        if (rem) {
            newduration++;
            rem--;
        }

        sd_stringparser_set_frame_duration(obj->animation_state.parser, i, newduration);
        sd_stringparser_peek(obj->animation_state.parser, i, &n);
        DEBUG("changed duration of frame %d from %d to %d", i, olddur, n.duration);
    }
}

void player_run(object *obj) {
    // Some vars for easier life
    player_animation_state *state = &obj->animation_state;
    player_sprite_state *rstate = &obj->sprite_state;
    if(state->finished) return;

    // Handle slide operation
    if(obj->slide_state.timer > 0) {
        obj->pos.x += obj->slide_state.vel.x;
        obj->pos.y += obj->slide_state.vel.y;
        obj->slide_state.timer--;
    }

    if(obj->enemy_slide_state.timer > 0) {
        obj->enemy_slide_state.duration++;
        obj->pos.x = state->enemy->pos.x + obj->enemy_slide_state.dest.x;
        obj->pos.y = state->enemy->pos.y + obj->enemy_slide_state.dest.y;
        obj->enemy_slide_state.timer--;
    }

    // Not sure what this does
    int run_ret;
    if(state->end_frame == UINT32_MAX) {
        run_ret = sd_stringparser_run(
            state->parser,
            state->ticks - 1);
    } else {
        run_ret = sd_stringparser_run_frames(
            state->parser,
            state->ticks - 1,
            state->end_frame);
    }

    // Handle frame
    if(run_ret == 0) {
        // Handle frame switch
        sd_stringparser_frame *param = &state->parser->current_frame;
        sd_stringparser_frame *f = param;
        int real_frame = param->letter - 65;

        // Do something if animation is finished!
        if(param->is_animation_end) {
            if(state->repeat) {
                player_reset(obj);
                sd_stringparser_run(state->parser, state->ticks - 1);
                real_frame = param->letter - 65;
            } else if(obj->finish != NULL) {
                obj->cur_sprite = NULL;
                obj->finish(obj);
                return;
            } else {
                obj->cur_sprite = NULL;
                state->finished = 1;
                return;
            }
        }

        state->entered_frame = 0;
        // If frame changed, do something
        if(param->id != state->previous) {
            player_clear_frame(obj);
            state->entered_frame = 1;

            // Tick management
            if(isset(f, "d")) {
                if(!obj->animation_state.disable_d) {
                    state->ticks = get(f, "d") + 1;
                    sd_stringparser_goto_tick(state->parser, state->ticks);
                }
            }

            // Hover flag
            if(isset(f, "h")) {
                rstate->disable_gravity = 1;
            } else {
                rstate->disable_gravity = 0;
            }

            if(isset(f, "ua")) {
                obj->animation_state.enemy->sprite_state.disable_gravity = 1;
            }

            // Animation management
            if(isset(f, "m") && state->spawn != NULL) {
                int mx = 0;
                if (isset(f, "mrx")) {
                    int mrx = get(f, "mrx");
                    int mm = isset(f, "mm") ? get(f, "mm") : mrx;
                    mx = random_int(&obj->rand_state, 320 - 2*mm) + mrx;
                    DEBUG("randomized mx as %d", mx);
                } else if(isset(f, "mx")) {
                    mx = obj->start.x + (get(f, "mx") * object_get_direction(obj));
                }

                int my = 0;
                if (isset(f, "mry")) {
                    int mry = get(f, "mry");
                    int mm = isset(f, "mm") ? get(f, "mm") : mry;
                    my = random_int(&obj->rand_state, 320 - 2*mm) + mry;
                    DEBUG("randomized my as %d", my);
                } else if(isset(f, "my")) {
                    my = obj->start.y + get(f, "my");
                }

                int mg = isset(f, "mg") ? get(f, "mg") : 0;
                /*DEBUG("Spawning %d, with g = %d, pos = (%d,%d)", */
                    /*get(f, "m"), mg, mx, my);*/
                state->spawn(
                    obj, get(f, "m"),
                    vec2i_create(mx, my), mg,
                    state->spawn_userdata);
            }
            if(isset(f, "md") && state->destroy != NULL) {
                state->destroy(obj, get(f, "md"), state->destroy_userdata);
            }

            // Music playback
            if(isset(f, "smo")) {
                if(get(f, "smo") == 0) {
                    music_stop();
                    return;
                }

                // Find file we want to play
                char *filename = NULL;
                switch(get(f, "smo")) {
                    case 1: filename = get_path_by_id(PSM_END); break;
                    case 2: filename = get_path_by_id(PSM_MENU); break;
                    case 3: filename = get_path_by_id(PSM_ARENA0); break;
                    case 4: filename = get_path_by_id(PSM_ARENA1); break;
                    case 5: filename = get_path_by_id(PSM_ARENA2); break;
                    case 6: filename = get_path_by_id(PSM_ARENA3); break;
                    case 7: filename = get_path_by_id(PSM_ARENA4); break;
                }
                if(filename != NULL) {
                    music_play(filename);
                    free(filename);
                    music_set_volume(settings_get()->sound.music_vol/10.0f);
                }
            }
            if(isset(f, "smf")) {
                music_stop();
            }

            // Sound playback
            if(isset(f, "s")) {
                float pitch = PITCH_DEFAULT;
                float volume = VOLUME_DEFAULT * (settings_get()->sound.sound_vol/10.0f);
                float panning = PANNING_DEFAULT;
                if(isset(f, "sf")) {
                    int p = clamp(get(f, "sf"), -16, 239);
                    pitch = clampf((p/239.0f)*3.0f + 1.0f, PITCH_MIN, PITCH_MAX);
                }
                if(isset(f, "l")) {
                    int v = clamp(get(f, "l"), 0, 100);
                    volume = (v / 100.0f) * (settings_get()->sound.sound_vol/10.0f);
                }
                if(isset(f, "sb")) {
                    panning = clamp(get(f, "sb"), -100, 100) / 100.0f;
                }
                int sound_id = obj->sound_translation_table[get(f, "s")] - 1;
                sound_play(sound_id, volume, panning, pitch);
            }

            // Blend mode stuff
            if(isset(f, "b1")) { rstate->method_flags &= 0x2000; }
            if(isset(f, "b2")) { rstate->method_flags &= 0x4000; }
            if(isset(f, "bb")) {
                rstate->method_flags &= 0x0010;
                rstate->blend_finish = get(f, "bb");
                rstate->screen_shake_vertical = get(f, "bb");
            }
            if(isset(f, "be")) { rstate->method_flags &= 0x0800; }
            if(isset(f, "bf")) {
                rstate->method_flags &= 0x0001;
                rstate->blend_finish = get(f, "bf");
            }
            if(isset(f, "bh")) { rstate->method_flags &= 0x0040; }
            if(isset(f, "bl")) {
                rstate->method_flags &= 0x0008;
                rstate->blend_finish = get(f, "bl");
                rstate->screen_shake_horizontal = get(f, "bl");
            }
            if(isset(f, "bm")) {
                rstate->method_flags &= 0x0100;
                rstate->blend_finish = get(f, "bm");
            }
            if(isset(f, "bj")) {
                rstate->method_flags &= 0x0400;
                rstate->blend_finish = get(f, "bj");
            }
            if(isset(f, "bs")) {
                rstate->blend_start = get(f, "bs");
            }
            if(isset(f, "bu")) { rstate->method_flags &= 0x8000; }
            if(isset(f, "bw")) { rstate->method_flags &= 0x0080; }
            if(isset(f, "bx")) { rstate->method_flags &= 0x0002; }

            // Palette tricks
            if(isset(f, "bpd")) { rstate->pal_ref_index = get(f, "bpd"); }
            if(isset(f, "bpn")) { rstate->pal_entry_count = get(f, "bpn"); }
            if(isset(f, "bps")) { rstate->pal_start_index = get(f, "bps"); }
            if(isset(f, "bpf")) {
                // Exact values come from master.dat
                if(game_state_get_player(obj->gs, 0)->har == obj) {
                    rstate->pal_start_index =  1;
                    rstate->pal_entry_count = 47;
                } else {
                    rstate->pal_start_index =  48;
                    rstate->pal_entry_count = 48;
                }
            }
            if(isset(f, "bpp")) {
                rstate->pal_end = get(f, "bpp") * 4;
                rstate->pal_begin = get(f, "bpp") * 4;
            }
            if(isset(f, "bpb")) { rstate->pal_begin = get(f, "bpb") * 4; }
            if(isset(f, "bz"))  { rstate->pal_tint = 1; }

            // The following is a hack. We don't REALLY know what these tags do.
            // However, they are only used in CREDITS.BK, so we can just interpret
            // then as we see fit, as long as stuff works.
            if(isset(f, "bc") && f->duration >= 50) {
                rstate->blend_start = 0;
            } else if(isset(f, "bd") && f->duration >= 30) {
                rstate->blend_finish = 0;
            }

            // Handle movement
            if(isset(f, "ox")) {
                DEBUG("changing X from %f to %f", obj->pos.x, obj->pos.x+get(f, "ox"));
                /*obj->pos.x += get(f, "ox");*/
            }

            if(isset(f, "oy")) {
                DEBUG("changing Y from %f to %f", obj->pos.y, obj->pos.y+get(f, "oy"));
                /*obj->pos.y += get(f, "oy");*/
            }

            if (isset(f, "bm")) {
                // hack because we don't have 'walk to other HAR' implemented
                obj->pos.x = state->enemy->pos.x;
                obj->pos.y = state->enemy->pos.y;
                player_next_frame(state->enemy);
            }

            if (isset(f, "v")) {
                int x = 0, y = 0;
                if(isset(f, "y-")) {
                    y = get(f, "y-") * -1;
                } else if(isset(f, "y+")) {
                    y = get(f, "y+");
                }
                if(isset(f, "x-")) {
                    x = get(f, "x-") * -1 * object_get_direction(obj);
                } else if(isset(f, "x+")) {
                    x = get(f, "x+") * object_get_direction(obj);
                }

                if (x || y) {
                    DEBUG("x vel %d, y vel %d", x, y);
                    obj->vel.x += x;
                    obj->vel.y += y;
                }
            }

            if (isset(f, "bu") && obj->vel.y < 0.0f) {
                float x_dist = dist(obj->pos.x, 160);
                // assume that bu is used in conjunction with 'vy-X' and that we want to land in the center of the arena
                obj->slide_state.vel.x = x_dist / (obj->vel.y*-2);
                obj->slide_state.timer = obj->vel.y*-2;
            }


            // handle scaling on the Y axis
            if(isset(f, "y")) {
                obj->y_percent = get(f, "y") / 100.0f;
            }
            if (isset(f, "e")) {
                // x,y relative to *enemy's* position
                int x = 0, y = 0;
                if(isset(f, "y-")) {
                    y = get(f, "y-") * -1;
                } else if(isset(f, "y+")) {
                    y = get(f, "y+");
                }
                if(isset(f, "x-")) {
                    x = get(f, "x-") * -1 * object_get_direction(obj);
                } else if(isset(f, "x+")) {
                    x = get(f, "x+") * object_get_direction(obj);
                }

                if (x || y) {
                    obj->enemy_slide_state.timer = param->duration;
                    obj->enemy_slide_state.duration = 0;
                    obj->enemy_slide_state.dest.x = x;
                    obj->enemy_slide_state.dest.y = y;
                    /*DEBUG("ENEMY Slide object %d for (x,y) = (%f,%f) for %d ticks. (%d,%d) %f, %%f",
                            obj->cur_animation->id,
                            obj->enemy_slide_state.vel.x,
                            obj->enemy_slide_state.vel.y,
                            param->duration, x, y, x_dist, y_dist);*/
                }
            }
            if (isset(f, "v") == 0 &&
                isset(f, "e") == 0 &&
                (isset(f, "x+") || isset(f, "y+") || isset(f, "x-") || isset(f, "y-"))) {
                // check for relative X interleaving
                int x = 0, y = 0;
                if(isset(f, "y-")) {
                    y = get(f, "y-") * -1;
                } else if(isset(f, "y+")) {
                    y = get(f, "y+");
                }
                if(isset(f, "x-")) {
                    x = get(f, "x-") * -1 * object_get_direction(obj);
                } else if(isset(f, "x+")) {
                    x = get(f, "x+") * object_get_direction(obj);
                }

                obj->slide_state.timer = param->duration;
                obj->slide_state.vel.x = (float)x;
                obj->slide_state.vel.y = (float)y;
                /*DEBUG("Slide object %d for (x,y) = (%f,%f) for %d ticks.",*/
                    /*obj->cur_animation->id,*/
                    /*obj->slide_state.vel.x, */
                    /*obj->slide_state.vel.y, */
                    /*param->duration);*/
            }

            if(isset(f, "x=") || isset(f, "y=")) {
                obj->slide_state.vel = vec2f_create(0,0);
            }
            if(isset(f, "x=")) {
                obj->pos.x = obj->start.x + (get(f, "x=") * object_get_direction(obj));
                sd_stringparser_frame n;
                int r;
                if((r =next_frame_with_tag(obj->animation_state.parser, f->id, "x=", &n)) >= 0) {
                    int next_x = get(&n, "x=");
                    int slide = obj->start.x + (next_x * object_get_direction(obj));
                    if(slide != obj->pos.x) {
                        obj->slide_state.vel.x = dist(obj->pos.x, slide) / (float)(param->duration + r);
                        obj->slide_state.timer = param->duration + r;
                        /*DEBUG("Slide object %d for X = %f for a total of %d ticks.",*/
                                /*obj->cur_animation->id,*/
                                /*obj->slide_state.vel.x,*/
                                /*param->duration + r);*/
                    }

                }
            }
            if(isset(f, "y=")) {
                obj->pos.y = obj->start.y + get(f, "y=");
                sd_stringparser_frame n;
                int r;
                if((r =next_frame_with_tag(obj->animation_state.parser, f->id, "y=", &n)) >= 0) {
                    int next_y = get(&n, "y=");
                    int slide = next_y + obj->start.y;
                    if(slide != obj->pos.y) {
                        obj->slide_state.vel.y = dist(obj->pos.y, slide) / (float)(param->duration + r);
                        obj->slide_state.timer = param->duration + r;
                        /*DEBUG("Slide object %d for Y = %f for a total of %d ticks.",*/
                                /*obj->cur_animation->id,*/
                                /*obj->slide_state.vel.y,*/
                                /*param->duration + r);*/
                    }

                }
            }
            if(isset(f, "as")) {
                // make the object move around the screen in a circular motion until end of frame
                obj->orbit = 1;
            } else {
                obj->orbit = 0;
            }
            if(isset(f, "q")) {
                // Enable hit on the current and the next n-1 frames.
                obj->hit_frames = get(f, "q");
            }
            if(obj->hit_frames > 0) {
                obj->can_hit = 1;
                obj->hit_frames--;
            }

            if(isset(f, "at")) {
                // set the object's X position to be behind the opponent
                obj->pos.x = obj->animation_state.enemy->pos.x + (15 * object_get_direction(obj));
            }

            if(isset(f, "ar")) {
                // reverse direction
                object_set_direction(obj, object_get_direction(obj) * -1);
            }


            // Set render settings
            if(real_frame < 25) {
                object_select_sprite(obj, real_frame);
                if(obj->cur_sprite != NULL) {
                    rstate->duration = param->duration;
                    rstate->blendmode = isset(f, "br") ? BLEND_ADDITIVE : BLEND_ALPHA;
                    if(isset(f, "r")) {
                        rstate->flipmode ^= FLIP_HORIZONTAL;
                    }
                    if(isset(f, "f")) {
                        rstate->flipmode ^= FLIP_VERTICAL;
                    }
                }
            } else {
                object_select_sprite(obj, -1);
            }

        }
        state->previous = param->id;
    }

    // Animation ticks
    if(state->reverse) {
        state->ticks--;
    } else {
        state->ticks++;
    }

    // Sprite ticks
    rstate->timer++;

    // All done.
    return;
}

void player_jump_to_tick(object *obj, int tick) {
    sd_stringparser_goto_tick(obj->animation_state.parser, tick);
    obj->animation_state.ticks = tick;
}

unsigned int player_get_len_ticks(object *obj) {
    return obj->animation_state.ticks_len;
}

void player_set_repeat(object *obj, int repeat) {
    obj->animation_state.repeat = repeat;
}

int player_get_repeat(object *obj) {
    return obj->animation_state.repeat;
}

void player_set_end_frame(object *obj, int end_frame) {
    obj->animation_state.end_frame = end_frame;
}

void player_next_frame(object *obj) {
    // right now, this can only skip the first frame...
    if(sd_stringparser_run(obj->animation_state.parser, 0) == 0) {
        obj->animation_state.ticks = obj->animation_state.parser->current_frame.duration + 1;
    }
}

void player_goto_frame(object *obj, int frame_id) {
    sd_stringparser_goto_frame(obj->animation_state.parser, frame_id, &obj->animation_state.ticks);
    obj->animation_state.ticks++;
}

int player_get_frame(object *obj) {
    return sd_stringparser_get_current_frame_id(obj->animation_state.parser);
}

char player_get_frame_letter(object *obj) {
    return sd_stringparser_get_current_frame_letter(obj->animation_state.parser);
}

const char* player_get_str(object *obj) {
    return obj->animation_state.parser->string;
}
