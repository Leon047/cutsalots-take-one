# How We Built the Boar Raid

*Cutsalots: Take One gameplay devlog · Team Sunshine · November 16, 2026*

When a boar comes near the garden in *Cutsalots: Take One*, a husky breaks off what it’s doing, turns toward the animal, and barks. The raid already had a cost in the village’s food stores. We gave the boar its own model, animations, and call so you could see and hear what was happening from the strategy camera.

## From a placeholder to an animated boar

The first boar was built from simple shapes. For the replacement, we used a reference made in ChatGPT and generated the textured mesh in Tripo. The tusks, low snout, and bristled back give the animal a silhouette you can pick out from above.

![Three-quarter view of the textured boar model with visible tusks and bristles](../../Art/Tripo/boar/check_34.png)

*A three-quarter model check shows the boar’s tusks, short legs, and raised bristles.*

In Blender, we rotated the mesh to face the right way and fitted the game’s existing bull skeleton to its four feet. We adjusted how much the legs, neck, and head pull on the mesh. The neck and head needed their own weights so the snout could dip when the boar feeds or lunges. In Unreal, we transferred seven bull animations to the new rig and checked how the boar stood, walked, galloped, ate, struck, reacted to a hit, and fell.

## What the husky does when it spots the boar

When the boar gets close, the husky stops moving or playing, turns to face it, and gives a loud alert bark. It keeps facing the boar and barks again roughly once a second until the animal is out of range. Then the dog can resume what it was doing.

![A husky stands beside the hall facing a boar on the path to the fenced vegetable beds](saga-scenes-devlog-images/husky-alerts-at-boar.jpg)

*The husky stops beside the hall and faces the boar as it approaches the garden.*

If the boar tramples the beds, crops disappear, the village loses 30 food, and the day’s harvest is halved. The boar now calls as it runs off. We attached the sound to the boar in Unreal so it follows the animal away from the garden.

## Back at the west gate

We also revisited the Day 1 spruce scene. In the **Living Saga**, the fog-born go straight for Hakon. He falls during the fight, and three extra fog-born join every wave on the final night. Hakon’s husky howls if it’s alive and present, and nearby villagers pause to mourn.

![Day 1 gameplay view with the fallen spruce across the west gate and the garden beside the hall](saga-scenes-devlog-images/west-gate-blocked.jpg)

*Day 1. The spruce blocks the forest road at the west gate.*

Ulf and the woodcutters work from the village side. The trunk shortens and cut logs appear as the job progresses. When the crew finishes, the route opens and 40 wood goes into the stockpile. In the **Ashen Saga**, the forest burned years ago, so Hakon walks home through a clear gate and helps defend it on the final night. The village gives up the 40 wood and its forest timber income is halved for the rest of the run.

![Gameplay view with villagers at the cleared west gate and a notification awarding 40 wood](saga-scenes-devlog-images/west-gate-cleared.jpg)

*The west gate is clear, and the notice confirms that 40 wood went to the stockpile.*

*Cutsalots: Take One was built for the 2026 Epic AI MegaJam by Team Sunshine.*
