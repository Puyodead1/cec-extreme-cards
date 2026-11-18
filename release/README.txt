Extreme Cards for Windows
=========================

Two builds of the Chuck E. Cheese "Extreme Cards" photo badge kiosk:

  Play-152.cmd   build 5.370.92.152 (2007)
  Play-170.cmd   build 5.370.92.170 (2008)

Start one with its Play-*.cmd (not the .exe directly: the .cmd sets up the data folder's links).
The attract screen appears after about 50 seconds.

Keys
  C            insert a coin
  1-8          the cabinet's buttons
  F11          fullscreen on/off (also Alt+Enter)

Camera: the first webcam is the booth camera. Set RT_CAMERA_DEVICE to part of a camera's name to pick
another one, RT_CAMERA_MIRROR=1 / RT_CAMERA_FLIP=1 to mirror or flip it, or RT_CAMERA to an image file
to use a still picture instead. You can also drop an image onto the window.

Prints: there is no card printer, so each finished card is saved as a PNG in the "prints" folder.

Operator setup (System Configure, edits vsystem.cfg): open a console in vend-152 or vend-170 and run
bin\nsyscfg.exe. Arrows/Enter move, F2 lists choices, F10 then y saves, Esc then y quits.

Log: vend-<build>\tmp\rt.log
