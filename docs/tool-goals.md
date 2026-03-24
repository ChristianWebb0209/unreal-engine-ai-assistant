# Tool Goals

These are a list of prompts, and a qualitative description of what the result should be. For our MVP, we expect that the AI assistant can perform ALL of these tasks.

---

## Tasks:

### Task 1: Build a simple collectible coin
**Prompt:**  
Build a simple scene with a coin object that rotates around and hovers up and down. When it collides with anything, it disappears and plays a sound.

**Expected Result:**  
- A static mesh representing a coin is added to the scene  
- A looping rotation animation is applied (likely via Tick or Timeline)  
- A sinusoidal up/down hover motion is implemented  
- Collision is enabled and triggers an overlap event  
- On overlap:
  - Coin is destroyed or hidden  
  - A sound cue is played at the coin’s location  

---

### Task 2: Create a basic player character
**Prompt:**  
Create a controllable third-person character with WASD movement and mouse camera control.

**Expected Result:**  
- A Character Blueprint is created  
- Movement input bindings are set up (WASD)  
- Camera is attached via a spring arm  
- Mouse input controls camera rotation  
- Character can move and rotate smoothly in the world  

---

### Task 3: Add a door that opens on interaction
**Prompt:**  
Create a door that opens when the player presses “E” near it.

**Expected Result:**  
- A door actor with a mesh and collision box  
- Player proximity detection via overlap  
- Input binding for "E" key  
- Smooth door rotation (timeline or interpolation)  
- Door only opens when player is nearby  

---

### Task 4: Spawn enemies periodically
**Prompt:**  
Create a system that spawns an enemy every 5 seconds at a fixed location.

**Expected Result:**  
- A spawner actor is created  
- Timer is configured to loop every 5 seconds  
- Enemy Blueprint is spawned at defined transform  
- Spawn logic is reusable and adjustable  

---

### Task 5: Basic enemy AI chase behavior
**Prompt:**  
Make an enemy that follows the player when they are within a certain distance.

**Expected Result:**  
- AI controller or simple movement logic  
- Distance check between enemy and player  
- Navigation system used for pathfinding  
- Enemy moves toward player when in range  
- Stops or idles when out of range  

---

### Task 6: Add a health system
**Prompt:**  
Implement a health system for the player that decreases when hit and triggers death at 0.

**Expected Result:**  
- Health variable added to player  
- Function to apply damage  
- Health clamps between 0 and max  
- Death event triggered at 0  
- Optional UI update hook  

---

### Task 7: Create a UI health bar
**Prompt:**  
Display the player’s health as a UI bar that updates in real time.

**Expected Result:**  
- Widget Blueprint created  
- Progress bar bound to player health  
- Widget added to viewport  
- Updates dynamically when health changes  

---

### Task 8: Add a pickup that increases health
**Prompt:**  
Create a health pickup that restores player health when collected.

**Expected Result:**  
- Pickup actor with mesh and collision  
- On overlap, increases player health  
- Value is configurable  
- Pickup is destroyed after use  
- Optional sound or visual effect  

---

### Task 9: Create a simple day-night cycle
**Prompt:**  
Implement a day-night cycle by rotating the directional light over time.

**Expected Result:**  
- Directional light rotates continuously  
- Speed is configurable  
- Lighting changes realistically over time  
- Optional skybox or atmosphere updates  

---

### Task 10: Add sound effects to footsteps
**Prompt:**  
Play footstep sounds when the player walks.

**Expected Result:**  
- Animation notifies or movement checks trigger sound  
- Sound cue plays at intervals while moving  
- Stops when player is idle  
- Optional variation in sound  

---

### Task 11: Save and load player position
**Prompt:**  
Create a system to save and load the player’s position.

**Expected Result:**  
- SaveGame class created  
- Player position stored on save  
- Loaded correctly on game start or trigger  
- Works consistently across sessions  

---

### Task 12: Add a simple projectile weapon
**Prompt:**  
Give the player a weapon that shoots projectiles when clicking.

**Expected Result:**  
- Input binding for mouse click  
- Projectile Blueprint created  
- Projectile spawns at weapon or camera  
- Moves forward with velocity  
- Detects collision and applies effects  

---

### Task 13: Create a minimap
**Prompt:**  
Add a minimap that shows the player’s position from a top-down view.

**Expected Result:**  
- SceneCaptureComponent2D used  
- Render target displayed in UI  
- Player icon visible and updates position  
- Camera positioned above player  

---

### Task 14: Add basic physics interaction
**Prompt:**  
Allow the player to push physics-enabled objects in the world.

**Expected Result:**  
- Physics simulation enabled on objects  
- Collision properly configured  
- Player movement applies force to objects  
- Objects respond naturally  

---

### Task 15: Implement a simple quest trigger
**Prompt:**  
Create a trigger zone that starts a quest when the player enters it.

**Expected Result:**  
- Trigger volume placed in level  
- On overlap, quest state changes  
- Event or UI notification fires  
- Trigger only activates once or as configured  

---