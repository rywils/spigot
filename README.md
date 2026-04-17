# Spigot
## A simple, hackable HTTP server

A lightweight, simple HTTP server written in C for quickly serving static files.


### Warning: Do not use in production of any kind

Designed for:

* spinning up a quick local server
* demoing basic web development to your friends who ask questions
- whenever you have a .html file and need to spin up a quick server
- basic testing

---

### What's built into it so far

* Serve files from any directory
* Runs on `localhost`
* Multithreaded (handles multiple requests)
* Optional live reload
* Basic HTTP support (GET only)
* MIME type detection
* Directory listing (if no `index.html`)
* Basic path safety protections
* Clean request logging

Add to it, or don't. That's up to you

---

## Build

Make sure you have `gcc` installed.

```bash
make
```

This will produce:

```bash
./spigot
```

---

## Usage

```bash
./spigot
```

Serve current directory on default port `8080`

---

```bash
./spigot <directory>
```

Serve a specific directory:

```bash
./spigot public
```

---

```bash
./spigot <port>
```

Serve current directory on custom port:

```bash
./spigot 3000
```

---

```bash
./spigot <directory> <port>
```

Serve directory on custom port:

```bash
./spigot public 3000
```

---

### Add to your path to trigger globally
```bash
chmod +x spigot
sudo install -m 755 spigot /usr/local/bin/spigot
```

Or add an alias (use your shell's config file (.bashrc, .zshrc, etc.))

```bash
echo 'alias serve="spigot"' >> ~/.zshrc
source ~/.zshrc
```

---

## Live Reload

Enable automatic browser refresh when files change:

```bash
./spigot --reload
```

or:

```bash
./spigot public 3000 --reload
```

### How it works

* Server scans for file changes
* Injects a small script into HTML pages
* Browser polls server for updates
* Page reloads automatically when changes are detected

---

## Directory Behavior

* If a directory contains `index.html` → it is served
* Otherwise → a basic file listing is generated

---

## Security Notes

Basic protections included:

* Blocks `..` (directory traversal)
* Sanitizes request paths
- Basic url decoding

### This is **not production-ready**

* No HTTPS
* No authentication
* No advanced request validation

---

## Supported File Types (for now)

* `.html` → `text/html`
* `.css` → `text/css`
* `.js` → `application/javascript`
* `.png` → `image/png`
* `.jpg/.jpeg` → `image/jpeg`
* default → `application/octet-stream`

---

## Example Output

```text
Serving directory: .
URL: http://localhost:8080
```

Request logs:

```text
[GET] /index.html → 200
[GET] /style.css → 200
[GET] /missing.txt → 404
```

---

## In the end

This project is intentionally:

* simple
* readable
* hackable

It is **not meant to replace full web servers**, but to:

* prototype quickly
* use for convenience only
* use when needed for something quick
* minor testing

---

## Limitations

* GET requests only
* No HTTP/2
* No persistent connections
* Thread-per-request model (not scalable)
* Live reload uses polling (not websockets)

---


## License

Use it, modify it, break it, learn from it. That's the entire point.

---

## ytho?

Because sometimes you just want:

```bash
./spigot
```

---

## …and we're done.
