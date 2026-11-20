import serial
import numpy as np
import pyqtgraph.opengl as gl
from pyqtgraph.Qt import QtWidgets, QtCore

ser = serial.Serial('COM3',115200,timeout=0.01)

app = QtWidgets.QApplication([])

view = gl.GLViewWidget()
view.show()
view.setWindowTitle('IMU orientation')

view.setCameraPosition(distance=120,elevation=20,azimuth=45)


# world axis
axis = gl.GLAxisItem()
axis.setSize(40,40,40)
view.addItem(axis)

# rectangle (sensor plate)
verts = np.array([
[-10,-25,0],
[ 10,-25,0],
[ 10, 25,0],
[-10, 25,0]
],dtype=float)

faces = np.array([
[0,1,2],
[0,2,3]
])

mesh = gl.GLMeshItem(
    vertexes=verts,
    faces=faces,
    smooth=False,
    drawEdges=True,
    edgeColor=(1,1,1,1),
    color=(0,0.6,1,0.7)
)

view.addItem(mesh)

# store original geometry
baseVerts = verts.copy()


def quat_to_rotmat(q0,q1,q2,q3):

    return np.array([

    [1-2*(q2*q2+q3*q3),2*(q1*q2-q0*q3),2*(q1*q3+q0*q2)],

    [2*(q1*q2+q0*q3),1-2*(q1*q1+q3*q3),2*(q2*q3-q0*q1)],

    [2*(q1*q3-q0*q2),2*(q2*q3+q0*q1),1-2*(q1*q1+q2*q2)]

    ])


def update():

    try:

        line = ser.readline().decode().strip()

        if not line:
            return

        parts = line.split(',')

        if len(parts) != 4:
            return

        q0,q1,q2,q3 = map(float,parts)
        print(f'{q0},{q1},{q2},{q3}')
        # normalize (safety)
        norm = np.sqrt(q0*q0+q1*q1+q2*q2+q3*q3)

        if norm < 0.5:
            return

        q0/=norm
        q1/=norm
        q2/=norm
        q3/=norm

        

        R = quat_to_rotmat(q0,q1,q2,q3)

        # rotate vertices relative to original shape
        rotated = baseVerts @ R.T

        mesh.setMeshData(
            vertexes=rotated,
            faces=faces,
            smooth=False,
            drawEdges=True,
            edgeColor=(1,1,1,1),
            color=(0,0.6,1,0.7)
        )

    except Exception as e:
        print(e)


timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start()

app.exec()