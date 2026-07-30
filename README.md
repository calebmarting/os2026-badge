# NOTE: A PURCHASE LINK IS IN THE WORKS AND WILL BE AVAILABLE SHORTLY, MORE INFO TO COME

# os2026-badge
Schematics and firmware for the Open Sauce 2026 Badge and SAOs

At least two people that we know of hacked the badge Saturday night, and showed up with 4 new games on Sunday! You'll find their code available in [the putnam hack folder.](examples/putnamhack/) And more instructions on how to install their code and the other examples [in the examples folder.](examples/)

We want to enable more people to make cool things after the event, so this repo contains everything that you need to make cool things with the badge and SAOs. This badge is just an Arduino MKR Zero in a Trench Coat so you can do what you want! Please post any cool makes and bring them to Open Sauce next year!

# Open Sauce 2026 General Badge Layout
General layout and pins for the Arduino MKR Zero

<img src="images/overview/badge_general_io.png" alt="General IO for the badge" width="600">

<img src="images/overview/badge_leds.png" alt="All of the LEDS on the badge" width="600">

<img src="images/overview/badge_ports.png" alt="Extra interfaces for the badge" width="600">

<img src="images/overview/badge_test_pads.png" alt="Extra pins for the MKR zero" width="600">

<img src="images/overview/badge_back.png" alt="Battery Holder for the Badge" width="600">

<img src="images/overview/sao_port_header.png" alt="SAO port header 3d print instructions" width="600">

The stl file to convert a regular 2x3 header can be found [here](https://www.printables.com/model/1568283-sao-simple-add-on-converter-adapter-for-2x3-pin-he)

# OpenSauce 2026 Badge Soldering Instructions

<img src="images/assembly/PXL_20260718_080234121.jpg" alt="Assembled badge" width="500">

### Before you start make sure you have:

- 1 Arduino Sopracciglio [T-Piece] (compatible with the Arduino MKR0)

<img src="images/assembly/PXL_20260718_055628908.jpg" alt="Arduino Sopracciglio" width="500">

- 1 Microphone CMA-4544PF-W

<img src="images/assembly/PXL_20260718_060135406.jpg" alt="Electret Microphone top side" height="300"> <img src="images/assembly/PXL_20260718_060150348.jpg" alt="Electret Microphone bottom side" height="300">

- 1 6x6mm Button (tactile switch)

<img src="images/assembly/PXL_20260718_060249253.jpg" alt="Tactile Switch top side" height="300"> <img src="images/assembly/PXL_20260718_060305084.jpg" alt="Tactile Switch bottom side" height="300">
- 3 5mm Red LEDs

<img src="images/assembly/PXL_20260718_060437870.jpg" alt="Three red LEDs" height="300">

- 1 Battery Holder BS-02-A1AJ010

<img src="images/assembly/PXL_20260718_060508420.jpg" alt="Battery holder top side" height="300"> <img src="images/assembly/PXL_20260718_070345305.jpg" alt="Battery Holder bottom side" height="300">

Optional:
- 2 6-Pin Headers
- 1 2-Pin Header

### Step 1
#### soldering the T-piece
Set the badge face up, and take off the Lanyard.

<img src="images/assembly/PXL_20260718_071340856.jpg" alt="OpenSauce Badge" width="500">

Carefully Align the T-Piece, checking that the gold pads on the edges are over the silver pads on the badge.

<img src="images/assembly/PXL_20260718_071358174.jpg" alt="OpenSauce Badge with Nose" width="500">

Solder the T-Piece to the badge.  Make sure you get all 21 pads.  We recommend soldering in this order:

<img src="images/assembly/solder_order.jpg" alt="Solder Order" width="500">

You may have to hold the T-Piece in place while you do the first joint or two. A friend could help hold it down. You want to completely fill the cutout on the edge of the T-Piece with solder and shoudl have a nice slope down to the badge pad.

<img src="images/assembly/castellated_solder.jpg" alt="Soldering Castellated edge detail" width="500">

### Step 2
#### soldering the microphone
The microphone goes over SauceBot's right eye.  Put it in from the face side, and make sure to get it centered over the pupil.

<img src="images/assembly/PXL_20260718_072837464.jpg" alt="Microphone placement" width="500">

Carefully turn the badge over while keeping the microphone tight agianst the badge. The leads should come out both holes.

<img src="images/assembly/PXL_20260718_073001936.jpg" alt="Microphone placement from the back" width="500">

Solder the microphone leads.

<img src="images/assembly/PXL_20260718_073500251.jpg" alt="Microphone soldered from back" width="500">

### Step 3
#### soldering the button
The button goes over SauceBot's left eye.  Put it in from the face side, and make sure it's aligned with the holes.  It will take a bit of force to push down flat agianst the badge.

<img src="images/assembly/PXL_20260718_073314459.jpg" alt="Button placement" width="500">

Turn the badge over while keeping the button in place. All 4 leads should stick out slightly from the badge. Solder all 4 leads of the button

<img src="images/assembly/PXL_20260718_073451360.jpg" alt="Button soldered from back" width="500">


### Step 4
#### soldering the LEDs
The 3 LEDs are inserted from the front.  Insert them so that the **LONGER LEG** goes in the **SQUARE HOLE**.

<img src="images/assembly/PXL_20260718_073628364.jpg" alt="LED orientation" width="500">

You can do each LED one at a time, or you can do all 3 at once, depending on how comfortable you are soldering.

<img src="images/assembly/PXL_20260718_073710183.jpg" alt="LED placement" width="500">

You might find it helps to bend the leads outward some to hold the LED in place while soldering.

<img src="images/assembly/PXL_20260718_074441271.jpg" alt="LED soldered" width="500">

After you have soldered all 3 LEDs you can clip off the excess lead length.  Use flushcut pliers, being careful not to cut too close and damage the board.

<img src="images/assembly/PXL_20260718_074702349.jpg" alt="Using Flushcutters" width="500">

### Step 5
#### soldering the battery holder

The battery holder is installed on the **BACK**, so turn the badge over and place it the outline.

<img src="images/assembly/PXL_20260718_075118615.jpg" alt="Battery holder placement" width="500">

Flip your badge over to the front and solder the battery holder in. Be especially careful where you hold the iron.  You don't want to melt any of your previous work.

<img src="images/assembly/PXL_20260718_075255190.jpg" alt="soldering iron orientation" width="500">

### Step 6
Install the battery

## You're done!

<img src="images/assembly/PXL_20260718_081111203.jpg" alt="Completed badge" width="500">

### Extra Credit
There are 2 6-pin headers on saucebot's ears for SAOs (Simple Add-Ons) you can add, as well as a 2-pin battery only add-on header.  They are not requred to finsh your badge, but there are some cool things you can find at OpenSauce 2026 to plug in and augment your badge.

Your badge is also Arduino Compatible.  We call it the Arduino Sopracciglio, but if you want to use it with Arduino IDE, you'll need to set your sketch up for an Arduino MKR Zero).  You can also find the Code for the badge in [Caleb Marting's GitHub](https://github.com/calebmarting/os2026-badge).

---

## How to Play the game

Your Badge comes with a fun game to play! There are 5 actions that can be performed:
1. Microphone: LED next to the mic will flash, blow into the microphone to complete the action
2. Accelerometer: The top-most LED on the badge will flash, shake or lightly slap the badge to trigger 
3. Button: The mic next to the 6mm button on the right will flash, press to complete the action
4. Swipe Left: The 5 LEDs above the mouth will move to the left, swipe from the right side to the left side of the mouth to complete
5. Swipe Right: The 5 LEDs above the mouth will move to the right, swipe from the left side to the right side of the mouth to complete

If you do the wrong action, or don't complete the action in time, it will let you know the game has ended and restart.

After 3 games that end with no user interaction, it the badge will go to sleep and leave the SAO ports powered on. There is a small LED that will flash every few seconds to let you know it's still alive. Press the mini reset button in the center of the T piece to wake it back up and start playing! If you don't want to waste power, turn the switch off (away from the USB port)

This code is a lot more complicated than it needs to be, I had higher aspirations than I ended up being able to accomplish at 3AM on Saturday of the event, so it's in a mostly unfinished state. I highly recommend trying the code in examples/putnamhack for a more flushed-out version.