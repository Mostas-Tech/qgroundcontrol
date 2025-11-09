#pragma once
#include <QtCore/QPointF>
#include <QPolygonF>
#include <QLineF>
#include <QList>
#include <vector>
#include <variant>
#include <stdexcept>

namespace geom {

// -------- 2D primitives --------
class Circle {
public:
    Circle(const QPointF& center, double radius);
    const QPointF& center() const;
    double radius() const;
    bool isValid() const;                 // radius > 0
private:
    QPointF m_center;
    double  m_radius{};
};

class Polygon {
public:
    explicit Polygon(std::vector<QPointF> vertices);
    const std::vector<QPointF>& vertices() const;
    bool empty() const;
    bool isValid() const;                 // >=3 vertices and non-zero area
private:
    std::vector<QPointF> m_vertices;
};

// A top-level fence that can be a circle or a polygon
class Fence {
public:
    enum class Type { Circle, Polygon };

    Fence(const Circle& c);
    Fence(const Polygon& p);

    Type type() const;
    const Circle&  asCircle()  const;
    const Polygon& asPolygon() const;
    bool isValid() const;                 // Valid if contained shape is valid
private:
    std::variant<Circle, Polygon> m_shape;
};

class TransectFence {
public:
    TransectFence() = delete;
    TransectFence(const Polygon& outer);
    const QPointF& start() const; 
    const QPointF& end() const;
    Polygon outerPolygon;
    QList<Polygon> subPolygons;
    void SplitFence(QPointF anchor, QLineF line);
    void UpdateStartEnd();
    void generateTransectLines();
    void clearTransectLines();
    
private:
    QPointF m_start;
    QPointF m_end;
};
bool operator==(const Fence& A, const Fence& B);
bool operator!=(const Fence& A, const Fence& B);

// -------- 3D helpers (lightweight) --------
struct Vec3 {
    double x{}, y{}, z{};
    Vec3() = default;
    Vec3(double X, double Y, double Z);
    Vec3 operator+(const Vec3&) const;
    Vec3 operator-(const Vec3&) const;
    Vec3 operator*(double s) const;
    Vec3 operator/(double s) const;
};

double dot(const Vec3& a, const Vec3& b);
Vec3   cross(const Vec3& a, const Vec3& b);
double norm(const Vec3& a);
Vec3   unit(const Vec3& a);

struct Line3D { Vec3 p; Vec3 d; };
struct Plane  { Vec3 p; Vec3 n; };

// -------- Engine --------
class GeometryEngine {
public:
    // Always returns two parallel tangents to the circle (one on each side)
    static std::vector<QPointF> parallelTangentsToCircle(
        const QPointF& linePoint, const QPointF& lineDir, const Circle& circle);

    // For fences:
    // - Circle: always two tangents (delegates to the function above)
    // - Polygon: returns exactly two supporting lines if polygon has thickness along the queried direction,
    //            otherwise returns {}.
    static std::vector<QPointF> parallelTangentsToFence(
        const QPointF& linePoint, const QPointF& lineDir, const Fence& fence);

    // Point-in-polygon (even–odd). If inclusive=true, points on edges are treated as inside.
    static bool pointInPolygon(const QPointF& p,
                               const QPolygonF& poly,
                               bool inclusive = true);

    // Overload for std::vector<QPointF>
    static bool pointInPolygon(const QPointF& p,
                               const std::vector<QPointF>& poly,
                               bool inclusive = true);
    static bool pointInCircle(const QPointF& p,
                              const Circle& circle);
    // --- NEW: Trim an infinite line (anchor + unit dir) by the nearest intersections with fences.
    // Returns a finite segment that stops at the first hit in each direction.
    // 'extent' is the fallback distance on each side if no fence is hit.
    static QLineF trimByFences(const QPointF& anchor,
                               const QPointF& dirUnit,
                               double extent,
                               const std::vector<Fence>& fences);

    // Overload for QList<Fence> to match Qt-heavy callsites.
    static QLineF trimByFences(const QPointF& anchor,
                               const QPointF& dirUnit,
                               double extent,
                               const QList<Fence>& fences);
    static std::optional<QPointF> intersection(const QLineF& line, const Fence& fence, const QPointF& anchor);

};

} // namespace geom
