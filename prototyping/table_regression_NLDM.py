"""
Fit and plot a single NLDM 2D lookup table (e.g. rise_transition, cell_rise, etc).

Model: delay/slew = c0 + c1*x + c2*y + c3*x*y + c4*x^2 + c5*y^2
where x = input transition (index_1), y = output load capacitance (index_2).
This is the standard bilinear+quadratic form used to approximate NLDM surfaces.
"""

import numpy as np
from scipy.optimize import curve_fit
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401 (registers 3d projection)

# ---- raw table data (rise_transition, related_pin A, XOR2) ----
index_1 = np.array([0.0100000000, 0.0230506000, 0.0531329000, 0.1224740000,
                     0.2823110000, 0.6507430000, 1.5000000000])  # input transition
index_2 = np.array([0.0005000000, 0.0011573800, 0.0026790600, 0.0062013800,
                     0.0143547000, 0.0332277000, 0.0769143000])  # output load cap

values = np.array([
    [0.0559991000, 0.0684585000, 0.0971430000, 0.1629645000, 0.3141736000, 0.6633058000, 1.4723042000],
    [0.0559564000, 0.0684065000, 0.0969287000, 0.1629742000, 0.3141490000, 0.6638274000, 1.4727028000],
    [0.0558953000, 0.0683034000, 0.0970937000, 0.1630153000, 0.3136869000, 0.6637068000, 1.4762130000],
    [0.0558532000, 0.0683361000, 0.0969092000, 0.1628033000, 0.3141826000, 0.6636764000, 1.4720821000],
    [0.0560695000, 0.0685496000, 0.0968821000, 0.1625735000, 0.3139443000, 0.6631236000, 1.4703802000],
    [0.0591639000, 0.0707668000, 0.0979003000, 0.1624643000, 0.3140978000, 0.6621517000, 1.4735394000],
    [0.0691211000, 0.0795006000, 0.1035307000, 0.1643065000, 0.3145804000, 0.6652780000, 1.4691667000],
])
# rows = index_1 (input transition), cols = index_2 (output load), matching Liberty convention

# ---- build flat (x, y) -> z training set ----
X, Y = np.meshgrid(index_1, index_2, indexing="ij")
x_flat = X.ravel()
y_flat = Y.ravel()
z_flat = values.ravel()


def model(xy, c0, c1, c2, c3, c4, c5):
    x, y = xy
    return c0 + c1 * x + c2 * y + c3 * x * y + c4 * x**2 + c5 * y**2


popt, _ = curve_fit(model, (x_flat, y_flat), z_flat)
c0, c1, c2, c3, c4, c5 = popt

z_pred = model((x_flat, y_flat), *popt)
residuals = z_flat - z_pred
ss_res = np.sum(residuals**2)
ss_tot = np.sum((z_flat - np.mean(z_flat)) ** 2)
r2 = 1 - ss_res / ss_tot
rmse = np.sqrt(np.mean(residuals**2))

print("Fitted model:")
print(f"  delay(x, y) = {c0:.6f} + {c1:.6f}*x + {c2:.6f}*y + {c3:.6f}*x*y "
      f"+ {c4:.6f}*x^2 + {c5:.6f}*y^2")
print(f"  R^2  = {r2:.6f}")
print(f"  RMSE = {rmse:.6e}")

# ---- plot: fitted surface + original data points ----
fig = plt.figure(figsize=(9, 7))
ax = fig.add_subplot(111, projection="3d")

xg = np.linspace(index_1.min(), index_1.max(), 60)
yg = np.linspace(index_2.min(), index_2.max(), 60)
Xg, Yg = np.meshgrid(xg, yg)
Zg = model((Xg, Yg), *popt)

ax.plot_surface(Xg, Yg, Zg, cmap="viridis", alpha=0.6, edgecolor="none")
ax.scatter(x_flat, y_flat, z_flat, color="red", s=35, label="Liberty table points")

ax.set_xlabel("Input transition (index_1) [ns]")
ax.set_ylabel("Output load cap (index_2) [pF]")
ax.set_zlabel("rise_transition [ns]")
ax.set_title("NLDM rise_transition fit (XOR2, pin A)")
ax.legend()

plt.tight_layout()
plt.show()
print("Saved plot to /mnt/user-data/outputs/nldm_fit.png")