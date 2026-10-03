import * as THREE from 'three';
import { mergeGroup } from './building-kit.js';
import { scene } from '../core/scene.js';
import { SITES } from './layout.js';
/* =========================================================
   WATER TOWER
========================================================= */

export const waterSystem =
{
    pumpOn:true,
    valveOn:true,
    pressure:.42,
    level:72
};


export const waterTower =
new THREE.Group();

waterTower.position.set(
    SITES.tower.x,
    0,
    SITES.tower.z
);


export const towerLegMaterial =
new THREE.MeshStandardMaterial({
    color:0x515e62,
    metalness:.7,
    roughness:.35
});


for(
    let i=0;
    i<4;
    i++
){

    const a =
    Math.PI / 4 +
    i * Math.PI / 2;

    const leg =
    new THREE.Mesh(

        new THREE.CylinderGeometry(
            .18,
            .22,
            8,
            10
        ),

        towerLegMaterial
    );

    leg.position.set(
        Math.cos(a)*3,
        4,
        Math.sin(a)*3
    );

    leg.castShadow =
    true;

    waterTower.add(
        leg
    );
}


export const tankBody =
new THREE.Mesh(

    new THREE.CylinderGeometry(
        4.2,
        4.2,
        6,
        24
    ),

    new THREE.MeshStandardMaterial({
        color:0x66777c,
        metalness:.7,
        roughness:.32
    })

);

tankBody.position.y =
9;

tankBody.castShadow =
true;

waterTower.add(
    tankBody
);


export const water =
new THREE.Mesh(

    new THREE.CylinderGeometry(
        3.9,
        3.9,
        4,
        24
    ),

    new THREE.MeshStandardMaterial({

        color:0x229bd2,

        transparent:true,

        opacity:.72,

        emissive:0x07537a,

        emissiveIntensity:.35

    })

);

water.position.y =
8;

waterTower.add(
    water
);


// The steelwork a tank on legs actually has: X-bracing between the four posts, hoop bands
// around the shell, a climbable ladder, a guard rail at the top, and a conical cap that
// sheds rain instead of letting it stand on the roof plate.
const legAt = i => {
  const a = Math.PI / 4 + i * Math.PI / 2;
  return new THREE.Vector3(Math.cos(a) * 3, 0, Math.sin(a) * 3);
};

function strut(from, to, r = 0.075) {
  const d = to.clone().sub(from);
  const m = new THREE.Mesh(
    new THREE.CylinderGeometry(r, r, d.length(), 6),
    towerLegMaterial
  );
  m.position.copy(from).addScaledVector(d, 0.5);
  m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.clone().normalize());
  m.castShadow = true;
  waterTower.add(m);
  return m;
}

for (let i = 0; i < 4; ++i) {
  const a = legAt(i);
  const b = legAt((i + 1) % 4);
  for (const y of [1.0, 4.5]) {
    strut(new THREE.Vector3(a.x, y, a.z), new THREE.Vector3(b.x, y + 3.5, b.z), 0.06);
    strut(new THREE.Vector3(b.x, y, b.z), new THREE.Vector3(a.x, y + 3.5, a.z), 0.06);
  }
  // horizontal girts at three levels
  strut(new THREE.Vector3(a.x, 7.7, a.z), new THREE.Vector3(b.x, 7.7, b.z), 0.06);
}

for (const y of [6.6, 9.0, 11.4]) {
  const band = new THREE.Mesh(
    new THREE.TorusGeometry(4.26, 0.09, 5, 26),
    towerLegMaterial
  );
  band.rotation.x = Math.PI / 2;
  band.position.y = y;
  waterTower.add(band);
}

{
  const foot = legAt(0);
  const dir = foot.clone().normalize();
  for (const s of [-0.26, 0.26]) {
    const rail = new THREE.Mesh(
      new THREE.CylinderGeometry(0.055, 0.055, 8.4, 5),
      towerLegMaterial
    );
    rail.position.set(foot.x + dir.z * s, 4.4, foot.z - dir.x * s);
    waterTower.add(rail);
  }
  for (let k = 0; k < 11; ++k) {
    const rung = new THREE.Mesh(
      new THREE.CylinderGeometry(0.045, 0.045, 0.6, 4),
      towerLegMaterial
    );
    rung.rotation.x = Math.PI / 2;
    rung.rotation.z = Math.PI / 2;
    rung.position.set(foot.x, 0.6 + k * 0.74, foot.z);
    waterTower.add(rung);
  }
}

const cap = new THREE.Mesh(
  new THREE.ConeGeometry(4.5, 1.5, 24),
  tankBody.material
);
cap.position.y = 12.7;
cap.castShadow = true;
waterTower.add(cap);

for (let k = 0; k < 10; ++k) {
  const a = (k / 10) * Math.PI * 2;
  const post = new THREE.Mesh(
    new THREE.CylinderGeometry(0.05, 0.05, 0.85, 4),
    towerLegMaterial
  );
  post.position.set(Math.cos(a) * 4.5, 12.0, Math.sin(a) * 4.5);
  waterTower.add(post);
}
const rail = new THREE.Mesh(new THREE.TorusGeometry(4.5, 0.055, 4, 26), towerLegMaterial);
rail.rotation.x = Math.PI / 2;
rail.position.y = 12.4;
waterTower.add(rail);


mergeGroup(waterTower, [water, tankBody]);

scene.add(
    waterTower
);
