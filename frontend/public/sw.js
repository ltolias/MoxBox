const cache_name = 'moxbox-sw-cache';

let fetch_timeout_ms = 60000;

self.addEventListener('install', event => {
    console.log('installing service worker')
    self.skipWaiting();
    event.waitUntil(caches.open(cache_name));
})

self.addEventListener ('activate', event => {
    console.log('activating service worker');
    event.waitUntil(self.clients.claim());
})
self.addEventListener('message', (event) => {
    if (event.data && 'fetch_timeout_ms' in event.data) {
      fetch_timeout_ms = event.data.fetch_timeout_ms
    }
});

const cached_extensions = ['gz', 'js', 'css', 'html', 'ico', 'png', 'jpg', 'heic', 'manifest', 'appcache']

message_client = (event, message) => {
    const clientId = event.resultingClientId !== "" ? event.resultingClientId : event.clientId;
    self.clients.get(clientId).then(client => client.postMessage(message));
}


let in_flight = 0

fetch_timeout_reported = async (event) => {
    const fetch_id = event.request.url//crypto.randomUUID();
    const fetch_start_ms = performance.now()
    in_flight = in_flight + 1;
    message_client(event, {in_flight, status: -1})
    try {
        const response = await fetch(event.request, {signal: AbortSignal.timeout(fetch_timeout_ms)})
        in_flight = in_flight - 1
        message_client(event, { in_flight, status: response.status, 
            elapsed_ms: performance.now() - fetch_start_ms 
        })
        return response;
    } catch (err) {
        console.error(`Service Worker Error: type=${err.name} message=${err.message}`);
        try {
            in_flight = in_flight - 1
            message_client(event, { in_flight, status: 503, 
                elapsed_ms: performance.now() - fetch_start_ms 
            })
        } catch(e2) {}

        throw(err)
    }
}  


self.addEventListener('fetch', (event) => {    

    const url_split = event.request.url.split('.')
    const file_extension = url_split[url_split.length - 1]

    whitelisted_urls = [self.location.origin + '/', self.location.origin + '/manifest.json']
    
    event.respondWith((async () => {
        const cache = await caches.open(cache_name);
        //if requesting '/' or whitelisted file/type, try to serve from sw cache, 
        if (cached_extensions.includes(file_extension) || whitelisted_urls.includes(event.request.url)) {
            const cached_response = await cache.match(event.request)
            if (cached_response) { 
                return cached_response
            } else { //otherwise, respond with fetched version and cache it
                const network_response = await fetch_timeout_reported(event);
                cache.put(event.request, network_response.clone());
                return network_response;
            }
        } else {
            // for other requests, dispatch normally
            return await fetch_timeout_reported(event)
        }
    })())
})