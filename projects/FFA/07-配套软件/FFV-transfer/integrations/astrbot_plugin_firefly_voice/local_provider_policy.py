"""Lifecycle-scoped SDK retry policy for this machine's Ollama only."""
from urllib.parse import urlsplit


class LocalProviderPolicy:
    def __init__(self):
        self.clients = []

    def apply(self, providers):
        for provider in providers:
            client = getattr(provider, 'client', None)
            if client is None or not hasattr(client, 'max_retries'):
                continue
            url = urlsplit(str(client.base_url))
            if (url.scheme != 'http' or url.hostname not in ('127.0.0.1', 'localhost', '::1')
                    or url.port != 11434):
                continue
            if any(existing is client for existing, _ in self.clients):
                continue
            self.clients.append((client, client.max_retries))
            client.max_retries = 0
        return len(self.clients)

    def restore(self):
        for client, original in self.clients:
            if client.max_retries == 0:
                client.max_retries = original
        self.clients.clear()
