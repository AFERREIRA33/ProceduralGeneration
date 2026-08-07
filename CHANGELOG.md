# Journal des versions — ProceduralGeneration

Moteur de terrain voxel destructible temps réel — Unreal Engine 5.6 / C++

Le projet suit le versionnage sémantique `MAJEUR.MINEUR.CORRECTIF`. Le numéro **majeur** change lorsqu'une évolution rompt la compatibilité des sauvegardes existantes, le **mineur** à l'ajout d'une fonctionnalité, le **correctif** à la seule correction de défauts.

Les entrées sont classées par nature : **Ajouts**, **Modifications**, **Corrections**.

Le format de sérialisation des modifications du joueur porte son propre numéro de version, indiqué à chaque entrée. Une archive dont la version ou la taille de monde ne correspond pas est rejetée au chargement, avec un avertissement en journal et régénération du monde depuis la graine.

---

## [1.1] — 24 juillet 2026

Version stable remise au commanditaire. Regroupe les correctifs de fiabilité issus de l'exploitation de la version 1.0.

**Format de sauvegarde : v3 — compatible avec les versions 0.6 à 1.0.**

### Ajouts

- Visualisation de debug de l'octree, activable par la propriété `bDebugDrawOctree` sur l'acteur générateur. Matérialise en jeu les nœuds chargés, colorés selon leur échelle, ainsi que la position de référence utilisée par le système de streaming.
- Suite de tests d'automation couvrant le noise procédural, les clés d'octree et le format d'archive des modifications. Exécutable depuis la Session Frontend de l'éditeur ou en ligne de commande.

### Modifications

- Budget de création de chunks rendu proportionnel au retard accumulé, entre le plancher de régime établi et le plafond de remplissage initial, au lieu d'une valeur fixe.
- Métrique de niveau de détail fondée sur la distance horizontale au joueur : le relief situé sous le joueur reste au détail maximal quelle que soit son altitude.
- Suppression du code devenu inatteignable — deux classes orphelines, une génération de plan de creusement remplacée par le système par tuiles, et les journaux de diagnostic sans usage.

### Corrections

- **B-05** — Le terrain cessait de se raffiner et le monde devenait fini au-delà d'environ huit chunks du point d'apparition ; les grottes n'apparaissaient plus hors de la zone de départ. Les acteurs orchestrateurs étaient déchargés en cours de session par le système World Partition du moteur. Ils sont désormais exclus du chargement spatial. (`8646a5d`)
- L'ensemble des nœuds souhaités n'était pas réinitialisé avant chaque recalcul, ce qui provoquait son accumulation au fil de la session et empêchait le déchargement des nœuds obsolètes. (`8646a5d`)
- **B-06** — Le composant de maillage signalait le rejet de triangles dégénérés à chaque reconstruction sur les zones planes. Les triangles dont deux sommets coïncident sont désormais écartés à l'émission, dans les cellules courantes comme dans les cellules de transition. (`8646a5d`)

### Notes d'exploitation

- L'exclusion des acteurs orchestrateurs du chargement spatial doit être reconduite sur toute nouvelle carte accueillant le générateur.
- Le niveau de détail horizontal augmente le nombre de nœuds fins chargés lors d'un survol à haute altitude. L'occupation mémoire relevée reste dans les seuils de supervision (4,4 à 6,6 Go).

**Commits :** `8646a5d`, `1260e25`

---

## [1.0] — 7 juin 2026

Première version stable. Périmètre fonctionnel complet, recette passée sans anomalie majeure ouverte.

**Format de sauvegarde : v3 — compatible avec les versions 0.6 et suivantes.**

### Ajouts

- Cellules de transition Transvoxel intégrées à l'octree : le nœud grossier rééchantillonne le champ de densité aux points du voisin fin, ce qui rend le raccord étanche sans accès aux données du voisin.
- Pipeline d'édition asynchrone : la reconstruction du maillage consécutive à une modification s'exécute hors du fil de jeu, sur un instantané des voxels, l'envoi au moteur étant réparti sur plusieurs trames.

### Modifications

- Optimisations de streaming et de performance sur le chemin de génération.

### Corrections

- Le trait de pinceau était discontinu aux frontières verticales entre nœuds d'octree, produisant une marche visible. L'opération est désormais propagée à tous les nœuds chevauchés par la sphère du pinceau, dimension verticale comprise, et le rayon minimal de l'outil a été relevé au-dessus du pas d'échantillonnage. (`90496ba`)

**Commits :** `90496ba`, `03618d3`

---

## [0.6] — 2 juin 2026

Introduction du niveau de détail et de la persistance. **Rupture du format de sauvegarde : introduction du format v3.**

### Ajouts

- Niveau de détail par sparse octree à nœuds de taille variable, avec déchargement différé : un nœud n'est détruit qu'une fois son remplaçant prêt.
- Sous-sol creusable et persistance des modifications du joueur, sérialisées par nœud dans `Saved/VoxelEdits/edits_<graine>.bin`.
- Grottes et creusement portés sur la structure octree.

### Corrections

- Fissures visibles aux transitions entre niveaux de détail, corrigées par des cellules de transition Transvoxel étanches, activées par défaut. (`f6a02d3`)

**Commits :** `5440003`, `f6a02d3`, `4486c95`

---

## [0.5] — 30 mai 2026

### Ajouts

- Grottes traversant les chunks, par worms déterministes calculés par tuile et assemblés sur un voisinage 3×3.
- Biomes selon un modèle température × humidité, avec mélange bilinéaire des couleurs.
- Hydrologie : océans par masque de continentalité et rivières creusées par noise dédié.
- Mise en forme du relief : redistribution exponentielle des hauteurs, accentuation des reliefs, décalage de plaine.
- Streaming asynchrone des chunks autour du joueur.

**Commit :** `9436e60`

---

## [0.4] — 23 mai 2026

### Ajouts

- Éditeur de terrain en temps réel : quatre modes (ajout, retrait, aplanissement, lissage), rayon et force réglables en jeu.
- Personnage volant piloté par Enhanced Input et HUD affichant l'état du pinceau et la performance.
- Découpage des chunks en sous-chunks, permettant de ne reconstruire que la portion de maillage affectée par une modification.

### Modifications

- Passage au rendu à facettes, la lisibilité du relief ayant été jugée insuffisante en rendu lissé.

### Corrections

- Le matériau n'était pas appliqué aux chunks générés. (`78e1a59`)

**Commits :** `06a7f4c`, `519e559`, `23e06f4`, `78e1a59`, `e7ce199`

---

## [0.3] — 8 janvier 2026

### Ajouts

- Génération de grottes par perlin worms et salles.
- Génération multi-chunks avec base multithread.
- Coordonnées de texture triplanaires.

### Corrections

- Sommets Marching Cubes mal placés sur les arêtes présentant un écart de densité quasi nul ; le cas dégénéré est désormais traité explicitement. (`b4981a5`)

**Commits :** `b4981a5`, `8d2d2d9`, `78d6283`

---

## [0.2] — 1er décembre 2025

### Ajouts

- Intégration de FastNoiseLite et génération de chunks à partir du champ de densité.
- Premier système de grottes, en génération asynchrone et infinie.

**Commit :** `26451b6`

---

## [0.1] — 22 octobre 2025

### Ajouts

- Mise en place du projet et de la classe de génération de surface.
- Première génération de heightmap.

**Commit :** `a6746fb`

---

## Correspondance des identifiants d'anomalies

Les identifiants `B-` sont ceux du plan de correction des bogues et des fiches de consignation des dossiers du projet. Les anomalies antérieures à la version 1.1 ont été corrigées avant la mise en place du suivi formel ; leur trace se lit dans les messages de commit préfixés `fix`.

| Identifiant | Version corrigée | Commit |
|---|---|---|
| B-01 — Interpolation Marching Cubes sur arête dégénérée | 0.3 | `b4981a5` |
| B-02 — Matériau non appliqué aux chunks générés | 0.4 | `78e1a59` |
| B-03 — Fissures aux transitions entre niveaux de détail | 0.6 | `f6a02d3` |
| B-04 — Trait de pinceau discontinu aux frontières verticales | 1.0 | `90496ba` |
| B-05 — Acteurs orchestrateurs déchargés par World Partition | 1.1 | `8646a5d` |
| B-06 — Triangles dégénérés émis par le mailleur Transvoxel | 1.1 | `8646a5d` |
