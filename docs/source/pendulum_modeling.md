# Modeling the Inverted Pendulum with Moving Cart

## System Overview

The inverted pendulum with a moving cart is a classic coupled mechanical system. Its purpose in this document is to describe the physical model, not the control strategy. The cart moves horizontally, while the pendulum rotates in a vertical plane about its attachment point. Because the cart and pendulum influence each other, the motion must be described with coupled nonlinear equations.

![Cart-pendulim](images/Cart-pendulum-background.png)

## Modeling Assumptions

To keep the physical model tractable, the task sheet uses the following assumptions:

1. **Frictionless motion**: The cart moves without friction, and its motion is limited to the horizontal axis.
2. **Rigid pendulum**: The pendulum rod does not bend or flex.
3. **Point masses**: The cart and pendulum mass are concentrated at idealized points.
4. **Planar motion**: The system moves only in a single vertical plane.

## Key Variables

| Variable | Meaning | Units |
|----------|---------|-------|
| $x$ | Horizontal position of the cart | meters |
| $\theta$ | Angle of the pendulum from the vertical | radians |
| $\dot{x}$ | Velocity of the cart | m/s |
| $\dot{\theta}$ | Angular velocity of the pendulum | rad/s |
| $\ddot{x}$ | Horizontal acceleration of the cart | m/s$^2$ |
| $\ddot{\theta}$ | Angular acceleration of the pendulum | rad/s$^2$ |
| $F$ | External force applied to the cart | N |
| $m_c$ | Mass of the cart | kg |
| $m_p$ | Mass of the pendulum | kg |
| $l$ | Length of the pendulum | m |
| $g$ | Gravitational acceleration | m/s$^2$ |
| $I_p$ | Moment of inertia of the pendulum about its center of mass | kg m$^2$ |

## How the Model is Derived

The equations of motion come from two standard ideas: Newton's second law for the cart and rotational dynamics for the pendulum. The important point is that the cart and pendulum are not independent. Motion of one changes the forces and torques acting on the other, which is why the final model is coupled.

### Linear Cart Acceleration

First, write the pendulum center of mass in terms of the cart position $x$ and the angle $\theta$:

$$
x_p = x + l\sin(\theta), \qquad y_p = l\cos(\theta)
$$


Taking time derivatives gives the pendulum center-of-mass accelerations:

$$
\ddot{x}_p = \ddot{x} + l\ddot{\theta}\cos(\theta) - l\dot{\theta}^2\sin(\theta)
$$

$$
\ddot{y}_p = -l\ddot{\theta}\sin(\theta) - l\dot{\theta}^2\cos(\theta)
$$

For the cart, the horizontal force balance is the applied force $F$ opposed by the horizontal reaction from the pendulum. If for example the pendulum falls to the right it will impart a reaction force on the cart to the left. If $H$ is that reaction force, then

$$
F - H = m_c\ddot{x}
$$


With the reaction force being defined as:

$$
-H = m_p\ddot{x}_p
$$

Substituting the expression for $\ddot{x}_p$ and rearranging gives the cart equation:

$$
\ddot{x} = -\frac{m_p l}{m_c + m_p} \ddot{\theta} \cos(\theta) - \frac{m_p l}{m_c + m_p} \dot{\theta}^2 \sin(\theta) + \frac{F}{m_c + m_p}
$$

This shows that the cart acceleration depends not only on the applied force, but also on the pendulum's angular acceleration and angular velocity.

### Angular Pendulum Acceleration

The pendulum rotates about the pivot point, not about its center of mass. That is why the effective rotational inertia is larger than $I_p$ alone. Using the parallel-axis theorem, the inertia about the pivot becomes

$$
I_p + m_p l^2
$$

The term $m_p l^2$ is the parallel-axis contribution. It comes from shifting the axis of rotation from the center of mass to the pivot. In simple terms, the mass is now rotating farther from the axis, so it resists angular acceleration more strongly.

The torque balance about the pivot can then be described as:

$$
\tau = (I_p + m_p l^2)\ddot{\theta}
$$

and the torque itself comes from gravity and from the cart's horizontal acceleration:

$$
\tau = m_p g l\sin(\theta) - m_p l\ddot{x}\cos(\theta)
$$

The gravity term tries to rotate the pendulum away from the vertical, while the cart acceleration term couples the cart motion back into the pendulum motion.

Combining these expressions gives the angular equation:

$$
\ddot{\theta} = \frac{m_p g l}{I_p + m_p l^2} \sin(\theta) - \frac{m_p l}{I_p + m_p l^2} \ddot{x} \cos(\theta)
$$

## Summary

The inverted pendulum model is a two-body, nonlinear, coupled system. Its behavior is captured by the two equations above, together with the physical assumptions and variable definitions. This model is the basis for simulation and analysis of the pendulum motion.

---
