The CreatiVision Keyboard project was built with MPLAB X IDE v6.25

The project folders here are intended for opening and configuring as projects in the MPLAB X IDE. 

CreatiVisionKeyboard_3.X folder contains the lastest (bidirectional PS/2) v3 project.
CreatiVisionKeyboard.X folder contains the original (unidirectional PS/2) v1 project.

To successfully recompile these projects, follow the following procedure:
1. Install the MPLAB X IDE for AVR 8-bit support with XC8 compiler (current version is 6.25).
2. Follow the installation steps to install the latest XC8 compiler (currently v3.0).
3. On starting MPLAB X, review any messages for Updates and Update Pack. Install all updates. 
4. Copy the entire required CreatiVision project folder into your MPLABXProjects folder.
5. Open the CreatiVision Project in MPLAB X IDE (ignore all current warnings / errors).
6. Click on MCC toolbar button, to open the Code Configurator.
7. Proceed (Finish) to install all detected required MCC downloads.
8. Click the "Generate" button, in the Project Resources panel.   
9. Perform a "Clean and Build Project" from the toolbar dropdown.

All going well, you should now have a successful build!

Be sure to also read the main.c source code header comments for other information, including the PCB version compatibility etc.

Have fun!
