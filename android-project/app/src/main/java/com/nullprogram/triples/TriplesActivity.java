package com.nullprogram.triples;

import org.libsdl.app.SDLActivity;

/**
 * Triples — Android launcher activity.
 *
 * SDL3's stock SDLActivity does all the actual UIView / OpenGL ES / touch
 * input wiring; we just need a subclass so the Java side knows which native
 * libraries to load. getLibraries() returns them in load order:
 *
 *   1. "SDL3"    → libSDL3.so   (the SDL3 framework, must come first)
 *   2. "triples" → libtriples.so (our C++ code, declared SHARED in
 *                                 the top-level CMakeLists' if(ANDROID)
 *                                 branch)
 *
 * AndroidManifest.xml's <activity android:name="..."> points here.
 */
public class TriplesActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "triples" };
    }
}
