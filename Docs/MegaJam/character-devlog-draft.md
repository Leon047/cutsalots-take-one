# From Axolotl Sketches to a Viking Village

*Cutsalots: Take One character devlog · Team Sunshine*

Hakon spends his first day in *Cutsalots: Take One* on the wrong side of the village gate. From the strategy camera, the feathered headband and bow help you pick him out. Getting that little axolotl from the reference art to the forest road meant teaching a short, broad, tailed character to move with animations made for a human Viking.

## Designing the villagers

We started with a set of furry axolotl references. The warrior became the model for the village workers. The four named villagers each borrowed a different look from the same set. Hakon the Hunter wears the archer’s feathered headband, Sigrun the Healer has the druid’s antler crown, Ulf the Woodcutter wears the blacksmith’s apron and goggles, and Torvald Shield-Bearer gets the jarl’s crown and fur collar. In the game, we made the named villagers slightly larger than the workers and gave the workers small variations in size and tint.

![Hakon’s original archer reference](../References/FurryAxolotlClasses/archer.png)

*The archer reference that became Hakon the Hunter.*

## Turning the art into a rig

We used ChatGPT to turn the villager references into front-facing T-poses. We kept details like the archer’s headband and the druid’s crown, took the weapons out of their paws, and removed capes that overlapped the arms. With the arms, hands, and legs clearly separated in the input image, Tripo Studio could generate the textured mesh and fit a humanoid rig. The empty paws also let us attach the game’s existing equipment in Unreal.

![Front-facing archer in a T-pose with empty paws](../References/FurryAxolotlClasses/tripo_input/archer_front.png)

*Hakon’s T-pose, the image we fed into Tripo Studio.*

The warrior was our first test. In Blender, we fixed the skin weights where Tripo had attached part of the tail to a thigh bone. Those vertices now follow the hips, so a stride doesn’t pull the tail along with the leg. Our first Unreal import also looked fine at rest and tore apart as soon as we played an animation. The mesh vertices were in centimeters and the bones were in meters. Exporting both in centimeters fixed it. After that, playing a clip became part of checking every import.

## Making the cast move

In Unreal Engine 5.9, we retargeted eleven existing Viking clips to each villager. The axolotls’ proportions changed how those clips read. We moved the arms outward to clear the warrior’s torso, curled the fingers around the handles, and shifted the shield toward the elbow. The named villagers needed their own grip adjustments too. We turned Hakon’s bow upright with the string toward him and slid Sigrun’s staff down in her paw.

![Warrior axolotl shown in a sheet of retargeted animation poses](../../Art/Tripo/axolotl_warrior/check_retarget_sheet.png)

*An Unreal animation check for the warrior, the first model we brought into the village.*

Once the warrior worked in a play session, we reused the Blender cleanup and Unreal import, retargeting, and equipment scripts for the rest of the cast. We checked the villagers during the workday and after they armed up at night, when the axe and shield grips mattered. The frog, ivory bird, and spotted amphibian raiders went through the Tripo pipeline from their own T-pose references and also use retargeted Viking clips.

Retargeting got the cast walking, carrying, and fighting within the jam schedule. Some motions still show their origins. Hakon’s inherited shooting clip barely raises his bow arm, and Sigrun’s staff can cross her antlers during an attack. Both moments show how much a character’s silhouette depends on the pose as well as the model.

*Cutsalots: Take One was built for the 2026 Epic AI MegaJam by Team Sunshine.*
