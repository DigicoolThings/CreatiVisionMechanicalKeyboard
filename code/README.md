The **CreatiVision Mechanical Keyboard** project was built with MPLAB X IDE v6.25

The project subfolders here are each intended for opening and configuring as seperate projects in the MPLAB X IDE. 

- **CreatiVisionKeyboard_3.X** folder contains the lastest v3 (bidirectional PS/2) project.
- **CreatiVisionKeyboard.X** folder contains the original v1 (unidirectional PS/2) project.

To successfully recompile these projects, please follow these steps:
1. Install the MPLAB X IDE for AVR 8-bit support with XC8 compiler (v6.25 at time of writing).
2. Follow the installation steps to install the latest XC8 compiler (v3.0 at time of writing).
3. On starting MPLAB X IDE, review any messages for *Updates* and *Update Packs*. Install all updates. 
4. Copy the entire required CreatiVision project subfolder into your own *MPLABXProjects* folder.
5. Open the CreatiVision Project in MPLAB X IDE.  Ignore all current warnings / errors!
6. Click on the **MCC** toolbar button, to open the Code Configurator.
7. Proceed (Finish) to install all detected required MCC downloads.
8. Click the "Generate" button, in the Project Resources panel.   
9. Finally, perform a "Clean and Build Project", found in the toolbar dropdown.

All going well, you should now have a successful build!

Be sure to also read the *main.c* source code header comments, for other information including the PCB version compatibility etc.

Have fun!
