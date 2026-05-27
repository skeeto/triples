// Service worker for Triples — cache-first, full-offline once installed.
//
// Bump CACHE_NAME (e.g. v1 → v2) on every release; the activate handler
// then sweeps any older cache so users get the new wasm/JS on next launch.
// Without this bump the SW happily serves stale assets forever.
const CACHE_NAME = 'triples-v1';
const ASSETS = [
    './',
    './index.html',
    './index.js',
    './index.wasm',
    './manifest.json',
    './triples-favicon.png',
    './triples-pwa-192.png',
    './triples-pwa-512.png',
];

// Install: pre-cache every asset so a cold offline launch (even right after
// first install) finds everything in the cache.
self.addEventListener('install', e => {
    e.waitUntil(
        caches.open(CACHE_NAME).then(c => c.addAll(ASSETS))
    );
    // Take control on first install rather than waiting for a page reload.
    self.skipWaiting();
});

// Activate: clear out any older versions of the cache.
self.addEventListener('activate', e => {
    e.waitUntil(
        caches.keys()
            .then(keys => Promise.all(
                keys.filter(k => k !== CACHE_NAME).map(k => caches.delete(k))
            ))
            .then(() => self.clients.claim())
    );
});

// Fetch: cache-first. Falls back to network on miss, which then populates
// the cache for next time.
self.addEventListener('fetch', e => {
    // Only GET — POSTs and friends pass through to the network unmodified.
    if (e.request.method !== 'GET') return;
    e.respondWith(
        caches.match(e.request).then(hit => {
            if (hit) return hit;
            return fetch(e.request).then(resp => {
                // Only cache successful, basic (same-origin) responses.
                if (!resp || resp.status !== 200 || resp.type !== 'basic') {
                    return resp;
                }
                const clone = resp.clone();
                caches.open(CACHE_NAME).then(c => c.put(e.request, clone));
                return resp;
            }).catch(() => hit);  // offline + cache miss → just fail
        })
    );
});
