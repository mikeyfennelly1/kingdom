# Technical Report: Docker Networking & DNS Resolution Issues
**Project:** Kingdom (C++20 Secure Messaging)
**Status:** Debugging Build Failures (`devbox install`)
**Date:** Monday, May 25, 2026

## 1. Problem Statement
During the Docker build process, the `devbox install` command consistently fails with a `context deadline exceeded` error. This occurs when the tool attempts to reach `https://cache.nixos.org` to verify package integrity.

### Symptoms:
*   **Timeout Window:** Failures occur exactly ~5 seconds after the network request initiates.
*   **Error Message:** `Error: Head "https://cache.nixos.org/[hash].narinfo": context deadline exceeded`.
*   **Scope:** Affects both the `builder` and `runtime` stages of the multi-stage Dockerfile.

## 2. Log Documentation & Debugging Timeline

### Attempt 1: Standard Build
*   **Action:** `docker compose build`
*   **Result:** Failure (Unauthorized).
*   **Finding:** Local Docker credentials had expired. Resolved via `docker logout`.

### Attempt 2: Credential-Free Build
*   **Action:** `docker build -t kingdom-app .`
*   **Result:** `context deadline exceeded`.
*   **Analysis:** Identified as a timeout. Initial hypothesis: transient network blip.

### Attempt 3: Retry with Plain Progress
*   **Action:** `docker build --progress=plain ...`
*   **Result:** Confirmed the 5-second timeout pattern.
*   **Discovery:** The error is bubbling up from the Go `http` client used by Devbox.

### Attempt 4: Verbose Debugging & Nix Config
*   **Action:** Modified Dockerfile to use `DEVBOX_DEBUG=1` and `NIX_CONFIG="connect-timeout = 30"`.
*   **Result:** Same 5-second failure.
*   **Log Insight:** 
    ```text
    time=... level=DEBUG msg="nix command starting" cmd.args="nix ... store info --json"
    ...
    cmd.stderr="cannot connect to socket at '/nix/var/nix/daemon-socket/socket': No such file or directory"
    ...
    Error: Head "https://cache.nixos.org/...": context deadline exceeded
    ```
*   **Conclusion:** The `nix` configuration change didn't help because Devbox's internal HTTP client is enforcing its own timeout before the underlying Nix process can finish its check.

## 3. DNS Resolution in Docker: A Deep Dive

When a command inside a Docker container (like `devbox`) attempts to resolve `cache.nixos.org`, it follows a specific path that is often the source of "deadline exceeded" errors.

### A. The Embedded DNS Server
Docker runs an internal DNS server at **`127.0.0.11`**. 
1.  The container's `/etc/resolv.conf` points exclusively to this address.
2.  If the request is for a container name on the same network, Docker resolves it locally.
3.  If the request is for an external domain (e.g., `cache.nixos.org`), the internal server forwards the request to the upstream DNS servers configured on the **host machine**.

### B. Common Failure Modes
1.  **Host-Container Mismatch:** If the host uses a local DNS stub resolver (like `systemd-resolved` at `127.0.0.53`), Docker cannot use that address (as it's a loopback). Docker will often fall back to Google's public DNS (`8.8.8.8`), which may be blocked by local firewalls.
2.  **IPv6 Overhead:** If the environment has IPv6 enabled but improperly routed, the DNS client may wait for an `AAAA` record response that never arrives, consuming the 5-second timeout window before falling back to IPv4.
3.  **NDOTS Configuration:** By default, Docker sets `ndots:5`. This means for any domain with fewer than 5 dots, it first tries to append the internal search domains. This can result in multiple failed DNS lookups before the actual query is sent to the internet.
4.  **MTU (Maximum Transmission Unit):** In some virtualized networks, the packet size allowed is smaller than the standard 1500 bytes. DNS responses (especially over UDP) that exceed the MTU will be silently dropped, causing the request to "hang" until the timeout.

## 4. Current Hypotheses
*   **Hypothesis A:** The 5-second timeout is a hardcoded limit in the specific version of `devbox` (0.17.0) for its initial "pre-flight" network checks.
*   **Hypothesis B:** DNS resolution is taking ~4.5 seconds due to `ndots` or IPv6 timeouts, leaving only 0.5 seconds for the actual HTTPS handshake, which then fails.

## 5. Recommended Next Steps
1.  **Explicit DNS:** Pass `--dns 8.8.8.8` to the build command to bypass host DNS glitches.
2.  **Network Host Mode:** Temporarily use `--network host` during the build to see if the Docker bridge network is the bottleneck.
3.  **MTU Reduction:** Force a lower MTU (e.g., 1400) in the Docker daemon or via build arguments.
