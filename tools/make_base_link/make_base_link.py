#!/usr/bin/env python3
"""
SolidWorks에서 부품별로 뽑은 STL들을 합쳐서 ROS용 base_link.STL을 만들고,
URDF에 넣을 숫자(바퀴 위치, 바퀴 간격, 충돌 박스, 관성)를 출력하는 스크립트.

추가 설치 없이 파이썬 3만 있으면 돼요 (윈도우·우분투 둘 다).

[준비] SolidWorks 어셈블리 -> 파일 > 다른 이름으로 저장 > STL > 옵션
    - 출력 형식 Binary, 단위 m, 해상도 거침
    - "STL 출력 데이터를 번역하지 않음" 체크
    - "어셈블리의 모든 부품을 단일 파일에 저장" 체크 해제   <- 부품마다 따로 저장
    - 출력 좌표계 "-- 기본 --"
  -> 한 폴더에 "어셈블리이름 - 부품이름-1.STL" 파일들이 생김

[실행 예]
    python3 make_base_link.py 폴더경로 --out ../../sumo_description/meshes/base_link.STL --mass 3.8

[좌표 변환]
    SolidWorks 어셈블리 좌표(이 로봇은 Y가 위, -X가 앞)를
    ROS 좌표(X 앞, Y 왼쪽, Z 위)로 돌리고,
    원점을 "바퀴들 중심의 평균 위치(바퀴 축 높이)"로 옮겨요.
    로봇을 다르게 조립했으면 --front / --up 옵션으로 방향을 바꿔요.
"""
import argparse
import glob
import os
import struct

AXES = {
    '+x': (1, 0, 0), '-x': (-1, 0, 0),
    '+y': (0, 1, 0), '-y': (0, -1, 0),
    '+z': (0, 0, 1), '-z': (0, 0, -1),
}


def load_stl(path):
    """바이너리 STL을 읽어서 삼각형 목록 [(v1, v2, v3), ...] 으로 돌려줌 (단위: 파일 그대로)."""
    with open(path, 'rb') as f:
        data = f.read()
    n = struct.unpack('<I', data[80:84])[0]
    if len(data) != 84 + 50 * n:
        raise ValueError(f'{path}: 바이너리 STL이 아니에요 (옵션에서 Binary로 저장하세요)')
    tris = []
    for i in range(n):
        v = struct.unpack('<9f', data[84 + 50 * i + 12: 84 + 50 * i + 48])
        tris.append(((v[0], v[1], v[2]), (v[3], v[4], v[5]), (v[6], v[7], v[8])))
    return tris


def bbox(tris):
    pts = [p for t in tris for p in t]
    mn = [min(p[k] for p in pts) for k in range(3)]
    mx = [max(p[k] for p in pts) for k in range(3)]
    return mn, mx


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def write_stl(path, tris, header):
    with open(path, 'wb') as f:
        f.write(header.encode('ascii', 'replace')[:80].ljust(80, b' '))
        f.write(struct.pack('<I', len(tris)))
        for a, b, c in tris:
            u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            w = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            nrm = cross(u, w)
            ln = dot(nrm, nrm) ** 0.5 or 1.0
            f.write(struct.pack('<12fH', nrm[0] / ln, nrm[1] / ln, nrm[2] / ln, *a, *b, *c, 0))


def main():
    ap = argparse.ArgumentParser(description='부품별 STL -> ROS base_link.STL + URDF 숫자')
    ap.add_argument('folder', help='부품별 STL이 들어 있는 폴더')
    ap.add_argument('--out', default='base_link.STL', help='만들 파일 (기본: base_link.STL)')
    ap.add_argument('--wheel', default='바퀴', help='바퀴 파일 이름에 들어간 글자 (기본: 바퀴)')
    ap.add_argument('--exclude', nargs='*', default=[], help='몸체에서 뺄 부품 이름 글자들')
    ap.add_argument('--front', default='-x', choices=AXES, help='SolidWorks에서 로봇 앞쪽 축 (기본: -x)')
    ap.add_argument('--up', default='+y', choices=AXES, help='SolidWorks에서 위쪽 축 (기본: +y)')
    ap.add_argument('--mass', type=float, default=3.8, help='로봇 전체 무게 kg (기본: 3.8)')
    ap.add_argument('--wheel-mass', type=float, default=0.1, help='바퀴 1개 무게 kg (기본: 0.1)')
    ap.add_argument('--hat', default='모자', help='충돌 박스 높이 계산에서 뺄 부품 글자 (기본: 모자)')
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.folder, '*.STL')) + glob.glob(os.path.join(args.folder, '*.stl')))
    files = [f for f in files if os.path.abspath(f) != os.path.abspath(args.out)]
    wheel_files = [f for f in files if args.wheel in os.path.basename(f)]
    body_files = [f for f in files if args.wheel not in os.path.basename(f)
                  and not any(e in os.path.basename(f) for e in args.exclude)]
    if len(wheel_files) < 2 or not body_files:
        raise SystemExit(f'바퀴 STL {len(wheel_files)}개, 몸체 STL {len(body_files)}개 — 폴더와 --wheel 이름을 확인하세요')

    # 1) 바퀴 중심 = 각 바퀴 STL 상자의 가운데
    wheels = {}
    for f in wheel_files:
        mn, mx = bbox(load_stl(f))
        wheels[os.path.basename(f)] = ([(mn[k] + mx[k]) / 2 for k in range(3)], mn, mx)
    C = [sum(w[0][k] for w in wheels.values()) / len(wheels) for k in range(3)]

    # 2) 회전: ROS X = 앞, ROS Z = 위, ROS Y = Z x X (왼쪽)
    X = AXES[args.front]
    Z = AXES[args.up]
    if dot(X, Z) != 0:
        raise SystemExit('--front 와 --up 은 서로 다른 축이어야 해요')
    Y = cross(Z, X)

    def to_ros(p):
        d = (p[0] - C[0], p[1] - C[1], p[2] - C[2])
        return (dot(X, d), dot(Y, d), dot(Z, d))

    # 3) 몸체 STL 합치기
    body = []
    nohat = []
    for f in body_files:
        t = [tuple(to_ros(p) for p in tri) for tri in load_stl(f)]
        body += t
        if args.hat not in os.path.basename(f):
            nohat += t
    write_stl(args.out, body,
              'base_link (ROS: x fwd, y left, z up; origin = wheel center, axle height)')

    # 4) URDF용 숫자 출력
    print(f'\n[완료] {args.out}  (삼각형 {len(body)}개, 몸체 부품 {len(body_files)}개)\n')
    print('[바퀴] base_link 기준 위치 (m) -> URDF joint <origin xyz="x y 0">')
    radii, widths, ys = [], [], []
    for name, (c, mn, mx) in sorted(wheels.items()):
        r = to_ros(c)
        ext = [abs(dot(a, (mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]))) for a in (X, Y, Z)]
        radii.append(ext[2] / 2)
        widths.append(ext[1])
        ys.append(r[1])
        pos = ('front' if r[0] > 0 else 'rear') + '_' + ('left' if r[1] > 0 else 'right')
        print(f'  {pos:12s} x={r[0]:+.4f}  y={r[1]:+.4f}  z={r[2]:+.4f}   <- {name}')
    sep = (sum(y for y in ys if y > 0) / max(1, len([y for y in ys if y > 0]))
           - sum(y for y in ys if y < 0) / max(1, len([y for y in ys if y < 0])))
    r_avg = sum(radii) / len(radii)
    print(f'\n  wheel_separation (좌우 간격) = {sep:.4f} m')
    print(f'  wheel_radius (반지름)       = {r_avg:.4f} m   (바퀴 폭 {sum(widths) / len(widths):.4f} m)')
    print(f'  바닥 높이 = {-r_avg:.4f} m  -> Gazebo 스폰 높이는 이보다 1 cm 정도 위로')

    mn, mx = bbox(nohat)
    size = [mx[k] - mn[k] for k in range(3)]
    ctr = [(mx[k] + mn[k]) / 2 for k in range(3)]
    m = args.mass - args.wheel_mass * len(wheels)
    print('\n[몸체 충돌 박스]  (모자거치대 제외)')
    print(f'  <origin xyz="{ctr[0]:.4f} {ctr[1]:.4f} {ctr[2]:.4f}"/>  <box size="{size[0]:.4f} {size[1]:.4f} {size[2]:.4f}"/>')
    print(f'  바닥에서 몸체 아래까지 = {(mn[2] + r_avg) * 1000:.1f} mm')
    print(f'\n[몸체 관성]  몸체 무게 = 전체 {args.mass} - 바퀴 {len(wheels)}x{args.wheel_mass} = {m:.3f} kg')
    print(f'  ixx="{m / 12 * (size[1] ** 2 + size[2] ** 2):.6f}" '
          f'iyy="{m / 12 * (size[0] ** 2 + size[2] ** 2):.6f}" '
          f'izz="{m / 12 * (size[0] ** 2 + size[1] ** 2):.6f}"')
    mw, rw, hw = args.wheel_mass, r_avg, sum(widths) / len(widths)
    print(f'\n[바퀴 관성]  바퀴 1개 {mw} kg, 원통 공식')
    print(f'  ixx="{mw / 12 * (3 * rw ** 2 + hw ** 2):.3e}" iyy="{0.5 * mw * rw ** 2:.3e}" '
          f'izz="{mw / 12 * (3 * rw ** 2 + hw ** 2):.3e}"\n')


if __name__ == '__main__':
    main()
