# Third-party notices

Liberty by Bada remains a native C++/Win32 application. Its floating-image feature ports the interaction and overlay-window lifecycle patterns described by these open-source projects into the existing single-EXE architecture:

- PinView — MIT License — https://github.com/Pragyanand/PinView
- TraceIt — MIT License — https://github.com/SigNeedsGit/TraceIt
- Screen Mask — Apache License 2.0 — https://github.com/didvc/screen-masking

The port uses Windows Imaging Component and `UpdateLayeredWindow`; it does not bundle Python, Electron, Node.js, Qt, or the projects' compiled binaries. Their original copyright and license terms remain with their respective authors.
