Please review this branch with parallel subagents. Spawn one subagent for
security risks, one for test gaps, and one for maintainability. Wait for all
three agents to finish, and then summarize the findings by category with file
references. write down the md in docs and state its name clearly. create the
md right away then append to it your findings


#######
Make a beautifully detailed web scene using js, of a subway station. It will be a stationary scene, so don't worry about movement, focus only on detail. It should be 3D and something that would be impressive to you. The scene must feature a brightness slider to control the lighting, as well as the ability to navigate around the scene using WASD  Then using this scene, turn it into an fps with humanoid/zombieoid enemies, visible ammo tracers, weapon recoil, muzzle flash, sfx and high detail. make it as sick fps existing game and make the station bigger and more realistic





Please review the codebase /Volumes/SSD5/work/Voxelfarm/BHP/voxel-farm-bhp/source/Cloud.App.React with parallel subagents. spawn one subagent for each task. write down a performance evaluation of loading of the app in docs/loading_evaluation_claude.md. Create the document right away and update it frequently. Be precise, don't invent issues, don't exaggerate issues. be very precise and report what you will see, will definitely improve the loading time. put the precise change in which file don't think too long. As soon as a subagent find something updat the document right away


Please review the codebase with parallel subagents. spwan one subagent for each task. one task for
security, one for performance and one for fast loading. Don't invent issues or hullicinate issues that 
is not there.  Wait for all three agents to finish, and then summarize the findings by category with file
references. Each category should be list recommendations according to priority, which file it concerns and to do-list on how to tacke this issue. Write down the md in docs and state its name clearly. create the
md right away then append to it your findings in docs folder.


I need to have a memory performance trace and log on com.voxelfarm.program.view.mesh in /Volumes/SSD5/work/Voxelfarm/BHP/voxel-farm-bhp/source/Cloud.Client.React/src most probably components/component-surface.js but I am not sure. the browser has 3.6 gb memory heap on the main thread. can we detect the cause of the main problem and write a detailed report on docs/all_meshes_memory.md. give me cause of problems, location in the files and solutions with priority and high impact low effort first. be precise and concise


analyse how alpha numeric attributes color legend is implemented and write     
 down docs/alpha_numeric.md with details so it can be used in cellworker mode 2 
 in the app                                                                     
                     
                     
#
Spawn a default subagent with fork_context=false, model=gpt-5.5, and reasoning_effort=high. Have it thoroughly review my uncommitted changes and provide a bug report. Do not interrupt mid-turn, wait for the findings.


#
Spawn an architect subagent and an engineer subagent. Instruct the architect to draft an implementation plan for the new auth flow. Instruct the engineer to build the scaffolding based strictly on the architect's output, and run npm run test when finished.



#
Spawn multiple subagents one for each task to implement /Volumes/SSD5/work/Voxelfarm/BHP/voxel-farm-bhp/source/Cloud.App.React/.brain/implementation_plan.md     


#

 analyse the /Volumes/SSD5/work/Voxelfarm/BHP/voxel-farm-bhp/source/Cloud.App.React/src/vendors/modules/
  Analysis.tsx loading. Spawn a default subagent with fork_context=false and reasoning_effort=high. Have
  it thoroughly review the loading and provide any issues or  bug report. Do not interrupt mid-turn, wait
  for the finding



#
in /Volumes/SSD5/work/Voxelfarm/BHP/voxel-farm-bhp/source/Cloud.Client.React/src/core/vf-truck-loader.js there is 135% cpu usage in the browser. 
create at do-list of each priority incrementaly and systematically, don't get distracted and don't read uneccessary files. some files are large read the portion that is important so you won't be overwhelmed    



## GTA Style 3D Racing Car Game
Create a single-file HTML, CSS, and JavaScript  GTA-style car racing game.

Requirements:

* Everything must be in one HTML file.
* Build a polished 3D GTA style driving game with a track, car, obstacles, checkpoints, timer, lap counter, speed display, and simple menu.
* Use arrow keys or WASD for steering, acceleration, braking, and reverse.
* Add smooth car physics with acceleration, drifting/sliding, friction, and collision with track barriers.
* Include at least one complete race track with turns, straightaways, grass/off-road areas, and visual details.
* Add collectibles or boost pads around the track.
* Add simple AI cars or ghost cars if possible, but prioritize making the player car feel good and the track playable.
* Include win/finish logic after 3 laps.
* Use CSS or Canvas only for all visuals.
* Clearly display your model family/name on the menu or HUD.
* Save the file in /Users/amgad/Desktop/Ai/<your_model_name>_race_game.html 
* Before finishing, review your own code, check that the car moves properly, collisions work, laps count correctly, and the game is playable. Fix obvious bugs.



## Halo 2-Style FPS
Create a single-file HTML, CSS, and JavaScript first-person browser game that recreates the playable feel of Halo 2-style multiplayer as closely as possible using only original assets.

Requirements:
- Everything must be in one HTML file.
- The main goal is a recognizable, playable early-2000s sci-fi arena shooter feel: floating platforms, towers, ramps, bridges, lower paths, vertical movement, long sightlines, glowing sci-fi details, and a cold metallic/snowy atmosphere.
- First-person camera with mouse-look pointer lock.
- WASD movement, jumping, gravity, collision, and basic physics.
- The map must render clearly and be playable/explorable without instantly breaking, clipping through everything, or falling forever.
- Add simple target bots or dummy enemies so the player has something to shoot at. They can be basic and do not need advanced AI.
- Add 2 or 3 simple weapons with original names/visuals inspired by common arena shooter roles: precision rifle, energy projectile, and close-range weapon.
- Include health/shield display, ammo, crosshair, basic pickups, simple score, and a small radar/minimap if possible.
- Use the Web Audio API to generate simple procedural sounds in JavaScript for shooting, impacts, pickups, menu clicks, and shield effects. Do not use audio files.
- Clearly display your model family/name on the main menu or HUD.
- Before finishing, review your own code, check that the map renders, movement works, weapons fire, and the player can actually explore the arena. Fix obvious bugs before final output.
- Prioritize a playable, recognizable sci-fi arena shooter over making the game overly complex.
- Save the file in /Users/amgad/Desktop/Ai/<your_model_name>_halo_game.html 