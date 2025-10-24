// ===============================
// geometry.cc — Implementation
// ===============================
#include "Decomposition.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <QPolygonF>
#include <QLineF>
#include <optional>   // added

namespace geom {

// ---- Circle ----
Circle::Circle(const QPointF& center, double radius) : m_center(center), m_radius(radius) {}
const QPointF& Circle::center() const { return m_center; }
double Circle::radius() const { return m_radius; }
bool Circle::isValid() const { return m_radius > 0.0; }

// ---- Polygon ----
Polygon::Polygon(std::vector<QPointF> vertices) : m_vertices(std::move(vertices)) {}
const std::vector<QPointF>& Polygon::vertices() const { return m_vertices; }
bool Polygon::empty() const { return m_vertices.empty(); }
bool Polygon::isValid() const {
    if (m_vertices.size() < 3) return false;
    long double a = 0.0L;
    const size_t n = m_vertices.size();
    for (size_t i=0, j=n-1; i<n; j=i++) {
        const long double x0 = m_vertices[j].x();
        const long double y0 = m_vertices[j].y();
        const long double x1 = m_vertices[i].x();
        const long double y1 = m_vertices[i].y();
        a += (x0*y1 - x1*y0);
    }
    return std::fabsl(a) > 1e-12L; // non-zero area
}

// ---- Fence ----
Fence::Fence(const Circle& c)  : m_shape(c) {}
Fence::Fence(const Polygon& p) : m_shape(p) {}
Fence::Type Fence::type() const { return std::holds_alternative<Circle>(m_shape) ? Type::Circle : Type::Polygon; }
const Circle&  Fence::asCircle()  const { return std::get<Circle>(m_shape); }
const Polygon& Fence::asPolygon() const { return std::get<Polygon>(m_shape); }
bool Fence::isValid() const {
    if (type() == Type::Circle) return asCircle().isValid();
    return asPolygon().isValid();
}

// ---- 3D helpers ----
Vec3::Vec3(double X,double Y,double Z):x(X),y(Y),z(Z){}
Vec3 Vec3::operator+(const Vec3& b) const { return {x+b.x,y+b.y,z+b.z}; }
Vec3 Vec3::operator-(const Vec3& b) const { return {x-b.x,y-b.y,z-b.z}; }
Vec3 Vec3::operator*(double s) const { return {x*s,y*s,z*s}; }
Vec3 Vec3::operator/(double s) const { return {x/s,y/s,z/s}; }

double dot(const Vec3& a,const Vec3& b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3   cross(const Vec3& a,const Vec3& b){ return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x }; }
double norm(const Vec3& a){ return std::sqrt(dot(a,a)); }
Vec3   unit(const Vec3& a){ double n=norm(a); if(n<1e-9) throw std::runtime_error("Zero-length vector"); return a/n; }

// ---- Engine: circle tangents ----
std::vector<QPointF> GeometryEngine::parallelTangentsToCircle(
    const QPointF& linePoint, const QPointF& lineDir, const Circle& circle)
{
    double L = std::hypot(lineDir.x(), lineDir.y());
    if (L < 1e-9) throw std::invalid_argument("Zero-length direction");
    QPointF v(lineDir.x()/L, lineDir.y()/L);
    QPointF u(-v.y(), v.x());

    const QPointF& C = circle.center();
    double r = circle.radius();

    double s_c = C.x()*u.x() + C.y()*u.y();
    double s1 = s_c + r;
    double s2 = s_c - r;

    auto build_anchor = [&](double s){
        // explicit in v/u coordinates: anchor = v*t0 + u*s
        double t0 = linePoint.x()*v.x() + linePoint.y()*v.y(); // projection on v
        return QPointF(v.x()*t0 + u.x()*s, v.y()*t0 + u.y()*s);
    };

    return { build_anchor(s1), build_anchor(s2) }; // always two lines
}

// ---- Engine: point in polygon ----
bool GeometryEngine::pointInPolygon(const QPointF& p, const QPolygonF& poly, bool inclusive)
{
    const int n = poly.size();
    if (n < 3) return false;

    auto onSeg = [](const QPointF& A, const QPointF& B, const QPointF& P){
        const double eps = 1e-12;
        const double cross = (B.x()-A.x())*(P.y()-A.y()) - (B.y()-A.y())*(P.x()-A.x());
        if (std::fabs(cross) > eps) return false;
        const double dot = (P.x()-A.x())*(B.x()-A.x()) + (P.y()-A.y())*(B.y()-A.y());
        if (dot < -eps) return false;
        const double len2 = (B.x()-A.x())*(B.x()-A.x()) + (B.y()-A.y())*(B.y()-A.y());
        if (dot - len2 > eps) return false;
        return true;
    };

    if (inclusive) {
        for (int i=0, j=n-1; i<n; j=i++) {
            if (onSeg(poly[j], poly[i], p)) return true;
        }
    }

    bool inside = false;
    for (int i=0, j=n-1; i<n; j=i++) {
        const QPointF& A = poly[j];
        const QPointF& B = poly[i];
        const bool cond = ((A.y() > p.y()) != (B.y() > p.y()));
        if (cond) {
            const double t = (p.y() - A.y()) / (B.y() - A.y());
            const double xint = A.x() + t * (B.x() - A.x());
            if (xint >= p.x()) inside = !inside;
        }
    }
    return inside;
}

bool GeometryEngine::pointInPolygon(const QPointF& p, const std::vector<QPointF>& poly, bool inclusive)
{
    if (poly.size() < 3) return false;
    QPolygonF qpoly; qpoly.reserve(static_cast<int>(poly.size()));
    for (const auto& q : poly) qpoly << q;
    return pointInPolygon(p, qpoly, inclusive);
}
bool GeometryEngine::pointInCircle(const QPointF& p, const Circle& circle)
{
    const QPointF& C = circle.center();
    double r = circle.radius();
    double dx = p.x() - C.x();
    double dy = p.y() - C.y();
    return (dx*dx + dy*dy) <= (r*r + 1e-12);
}
std::optional<QPointF> GeometryEngine::intersection(const QLineF& line, const Fence& fence, const QPointF& anchor)
{
    const QPointF p0 = line.p1();
    const QPointF p2 = line.p2();
    const double dx = p2.x() - p0.x();
    const double dy = p2.y() - p0.y();

    const double a = dx*dx + dy*dy;
    if (a < 1e-18) {
        // zero-length direction -> no valid intersection
        return std::nullopt;
    }

    const double EPS_DENOM = 1e-15;
    const double EPS_PT = 1e-9;

    if (fence.type() == Fence::Type::Circle) {
        // check anchor inside circle
        const Circle& circle = fence.asCircle();
        const QPointF& center2D = circle.center();
        double radius = circle.radius();

        const double fx = p0.x() - center2D.x();
        const double fy = p0.y() - center2D.y();

        const double b = 2.0 * (dx*fx + dy*fy);
        const double c = fx*fx + fy*fy - radius*radius;

        const double disc = b*b - 4.0*a*c;
        if (disc < 0.0) return std::nullopt; // no real intersection

        const double sq = std::sqrt(std::max(0.0, disc));
        const double t1 = (-b - sq) / (2.0*a);
        const double t2 = (-b + sq) / (2.0*a);

        std::vector<QPointF> hits;
        if (t1 >= 0.0) hits.emplace_back(p0.x() + dx * t1, p0.y() + dy * t1);
        if (t2 >= 0.0) {
            QPointF p(p0.x() + dx * t2, p0.y() + dy * t2);
            // avoid duplicate when tangent (t1 ~= t2)
            bool dup = false;
            for (const auto& hp : hits) {
                if (std::hypot(hp.x()-p.x(), hp.y()-p.y()) <= EPS_PT) { dup = true; break; }
            }
            if (!dup) hits.push_back(p);
        }

        if (hits.empty()) return std::nullopt;
        if (hits.size() == 1) return hits.front();

        // two or more: choose closest to anchor
        auto best = std::min_element(hits.begin(), hits.end(), [&](const QPointF& A, const QPointF& B){
            return std::hypot(A.x()-anchor.x(), A.y()-anchor.y()) < std::hypot(B.x()-anchor.x(), B.y()-anchor.y());
        });
        return *best;
    } else {
        // polygon case: intersect ray p0 + t*(dx,dy), t>=0 with each edge P + u*(ex,ey), u in [0,1]
        const auto& poly = fence.asPolygon().vertices();
        const size_t n = poly.size();
        if (n < 2) return std::nullopt;

        auto cross2 = [](double ax, double ay, double bx, double by) {
            return ax * by - ay * bx;
        };

        std::vector<QPointF> hits;

        for (size_t i = 0; i < n; ++i) {
            const QPointF& P = poly[i];
            const QPointF& Q = poly[(i+1) % n];
            const double sx = Q.x() - P.x();
            const double sy = Q.y() - P.y();

            const double denom = cross2(dx, dy, sx, sy);
            const double px = P.x() - p0.x();
            const double py = P.y() - p0.y();

            if (std::fabs(denom) < EPS_DENOM) {
                // parallel (or nearly) -> skip (no proper intersection or collinear)
                continue;
            }

            const double t = cross2(px, py, sx, sy) / denom;
            const double u = cross2(px, py, dx, dy) / denom;

            if (t >= 0.0 && u >= -1e-12 && u <= 1.0 + 1e-12) {
                QPointF ip(p0.x() + dx * t, p0.y() + dy * t);
                // deduplicate near-equal points
                bool dup = false;
                for (const auto& hp : hits) {
                    if (std::hypot(hp.x()-ip.x(), hp.y()-ip.y()) <= EPS_PT) { dup = true; break; }
                }
                if (!dup) hits.push_back(ip);
            }
        }

        if (hits.empty()) return std::nullopt;
        if (hits.size() == 1) return hits.front();

        // multiple intersections: pick closest to anchor
        auto best = std::min_element(hits.begin(), hits.end(), [&](const QPointF& A, const QPointF& B){
            return std::hypot(A.x()-anchor.x(), A.y()-anchor.y()) < std::hypot(B.x()-anchor.x(), B.y()-anchor.y());
        });
        return *best;
    }
}
// ---- Engine: fence tangents ----
std::vector<QPointF> GeometryEngine::parallelTangentsToFence(
    const QPointF& linePoint, const QPointF& lineDir, const Fence& fence)
{
    double L = std::hypot(lineDir.x(), lineDir.y());
    if (L < 1e-9) throw std::invalid_argument("Zero-length direction");
    QPointF v(lineDir.x()/L, lineDir.y()/L);
    QPointF u(-v.y(), v.x());

    if (fence.type() == Fence::Type::Circle) {
        return parallelTangentsToCircle(linePoint, v, fence.asCircle());
    }

    const auto& poly = fence.asPolygon().vertices();
    if (poly.size() < 3) return {};

    auto dot2 = [&](const QPointF& a, const QPointF& b){ return a.x()*b.x() + a.y()*b.y(); };

    double s_min = std::numeric_limits<double>::infinity();
    double s_max = -std::numeric_limits<double>::infinity();
    for (const auto& P : poly) {
        double s = dot2(P, u);
        s_min = std::min(s_min, s);
        s_max = std::max(s_max, s);
    }

    if (s_max - s_min <= 1e-12) return {}; // zero thickness, not valid

    auto build_anchor = [&](double s_k){
        // explicit in v/u coordinates: anchor = v*t0 + u*s_k
        double t0 = dot2(linePoint, v); // projection on v
        return QPointF(v.x()*t0 + u.x()*s_k, v.y()*t0 + u.y()*s_k);
    };

    return { build_anchor(s_min), build_anchor(s_max) }; // exactly two lines
}

// --- Fence comparison operators ---
static bool almostEqual(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps;
}
static bool pointEqual(const QPointF& A, const QPointF& B, double eps = 1e-9) {
    return almostEqual(A.x(), B.x(), eps) && almostEqual(A.y(), B.y(), eps);
}

bool operator==(const Fence& A, const Fence& B) {
    if (A.type() != B.type()) return false;
    if (A.type() == Fence::Type::Circle) {
        const Circle& a = A.asCircle();
        const Circle& b = B.asCircle();
        return pointEqual(a.center(), b.center()) && almostEqual(a.radius(), b.radius());
    } else {
        const auto& va = A.asPolygon().vertices();
        const auto& vb = B.asPolygon().vertices();
        if (va.size() != vb.size()) return false;
        for (size_t i = 0; i < va.size(); ++i) {
            if (!pointEqual(va[i], vb[i])) return false;
        }
        return true;
    }
}

bool operator!=(const Fence& A, const Fence& B) {
    return !(A == B);
}

void TransectFence::SplitFence(QPointF anchor, QLineF line) {}

void TransectFence::UpdateStartEnd() {}

void TransectFence::generateTransectLines() {}

void TransectFence::clearTransectLines() {}

}  // namespace geom
