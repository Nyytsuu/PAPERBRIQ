export function connectLiveStream(channels, handlers = {}) {
  if (typeof window === "undefined" || typeof EventSource === "undefined") {
    return () => {};
  }

  const channelList = Array.isArray(channels) ? channels.join(",") : String(channels || "");
  const stream = new EventSource(`/api/live?channels=${encodeURIComponent(channelList)}`);

  function handleSnapshot(event) {
    try {
      handlers.onSnapshot?.(JSON.parse(event.data));
    } catch (error) {
      handlers.onError?.("Unable to read live update.");
    }
  }

  function handleStreamError(event) {
    try {
      const payload = JSON.parse(event.data);
      handlers.onError?.(payload.message || "Live stream error.");
    } catch (error) {
      handlers.onError?.("Live stream error.");
    }
  }

  stream.addEventListener("open", () => handlers.onOpen?.());
  stream.addEventListener("snapshot", handleSnapshot);
  stream.addEventListener("stream-error", handleStreamError);
  stream.addEventListener("error", () => handlers.onError?.("Live stream reconnecting..."));

  return () => {
    stream.removeEventListener("snapshot", handleSnapshot);
    stream.removeEventListener("stream-error", handleStreamError);
    stream.close();
  };
}
