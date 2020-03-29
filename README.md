# SilentPatch for Scarface

Scarface: The World is Yours is one of these games which nowadays are unplayable out of the box.
Graphical corruptions present on nearly all modern machines makes the game impossible to play without using
unofficial fixes. However, until now those were working the issue around and could result in slightly degraded
performance.

SilentPatch for Scarface fixes this issue in the least invasive way possible, and comes with an array of improvements
to the game's performance. In the best case, those fixes might **double** game's performance and eliminate
most of the hitches present when driving around!

## Featured fixes

* Game-breaking graphical corruptions have been fixed, making the game playable on modern multicore machines
* Allowed the game to use all CPU cores (instead of locking to one core), dramatically improving performance
* Removed an unneeded multithreaded flag from the D3D device, possibly improving performance slightly
* Introduced a cache for some D3D resources used by the game, dramatically reducing the amount of stutter when roaming around the city
* Made the game list all selectable resolutions instead of a cherry picked list
* Moved game's settings from the Registry to settings.ini in game directory - this resolves possible issues with saving settings

![preview](https://i.imgur.com/XnNcJpM.png)