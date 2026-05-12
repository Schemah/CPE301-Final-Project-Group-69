# CPE301-Final-Project-Group-69
ATmega2560 ultrasonic system with LCD, buzzer, and state machine. 

Group members:
Adam Santos
Edrei Bugtong

Overview:
This system uses a ultrasonic sensor on a walking stick to measure distance. It has four states:
- Off
- Idle
- Error
- Active

This system used LED indicators, a buzzer for feedback, and LCD display, and UART logging

System Behavior:

Off:
- Inactive
- All outputs are disabled

Idle:
- System running, no object detected within range

Active:
- Object detected within threshold distance
- buzzer frequency increase as object gets closer

Error:
- Triggered when sensor readings fail or give bad output
- use reset button to go back to Idle state


Controls:
- ON Button → Starts system (Off → Idle)
- OFF Button → Immediately shuts system down
- RESET Button → Clears Error state
