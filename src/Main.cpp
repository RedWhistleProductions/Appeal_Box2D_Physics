#include <algorithm>
#include <box2d/box2d.h>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
struct Touch {
  std::string Other, Tag, Type;
  bool Sensor = false;
  float X = 0, Y = 0, Speed = 0;
};
struct Body {
  std::string Name, Tag;
  b2BodyId Id = b2_nullBodyId;
  b2ShapeId Shape = b2_nullShapeId;
  bool Sensor = false;
  float Friction = .3f, Restitution = 0, Density = 1;
  uint64_t Category = 1, Mask = UINT64_MAX;
  std::vector<Touch> Contacts, Events, Pending;
};
std::map<std::string, Body> Bodies;
b2WorldId World = b2_nullWorldId;
float Units = 1;
void Ensure() {
  if (B2_IS_NON_NULL(World))
    return;
  auto d = b2DefaultWorldDef();
  d.gravity = {0, -9.8f};
  World = b2CreateWorld(&d);
}
Body &Find(const std::string &n) {
  auto i = Bodies.find(n);
  if (i == Bodies.end())
    throw std::runtime_error("Physics body not attached: " + n);
  return i->second;
}
void Finite(float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y))
    throw std::runtime_error("Non-finite physics value");
}
float Number(const std::string &s) {
  size_t used;
  float n = std::stof(s, &used);
  Finite(n, 0);
  if (used != s.size())
    throw std::runtime_error("Invalid physics number");
  return n;
}
Body *Owner(b2ShapeId s) {
  return b2Shape_IsValid(s)
             ? static_cast<Body *>(b2Body_GetUserData(b2Shape_GetBody(s)))
             : nullptr;
}
void Add(Body &a, Body &b, bool sensor, float x = 0, float y = 0) {
  if (std::none_of(a.Contacts.begin(), a.Contacts.end(),
                   [&](const Touch &c) { return c.Other == b.Name; }))
    a.Contacts.push_back({b.Name, b.Tag, "", sensor, x, y, 0});
}
void Pair(b2ShapeId sa, b2ShapeId sb, const char *type, bool sensor,
          float x = 0, float y = 0, float speed = 0) {
  auto *a = Owner(sa);
  auto *b = Owner(sb);
  if (!a || !b)
    return;
  a->Events.push_back({b->Name, b->Tag, type, sensor, -x, -y, speed * Units});
  b->Events.push_back({a->Name, a->Tag, type, sensor, x, y, speed * Units});
}
b2ShapeDef Definition(Body &b) {
  auto d = b2DefaultShapeDef();
  d.density = b.Density;
  d.material.friction = b.Friction;
  d.material.restitution = b.Restitution;
  d.isSensor = b.Sensor;
  d.enableSensorEvents = true;
  d.enableContactEvents = true;
  d.enableHitEvents = true;
  d.filter.categoryBits = b.Category;
  d.filter.maskBits = b.Mask;
  return d;
}
void ClearShape(Body &b) {
  if (b2Shape_IsValid(b.Shape))
    b2DestroyShape(b.Shape, true);
  b.Shape = b2_nullShapeId;
}
} // namespace
extern "C" void Constructor() { Ensure(); }
extern "C" void Destructor() {
  if (B2_IS_NON_NULL(World))
    b2DestroyWorld(World);
  World = b2_nullWorldId;
  Bodies.clear();
  Units = 1;
}
extern "C" void Debug(bool) {}
extern "C" void Set_Paths(std::string) {}
extern "C" void Attach(std::string name) {
  Ensure();
  if (Bodies.count(name))
    return;
  auto &b = Bodies[name];
  b.Name = name;
  auto d = b2DefaultBodyDef();
  d.type = b2_dynamicBody;
  d.userData = &b;
  d.motionLocks.angularZ = true;
  b.Id = b2CreateBody(World, &d);
}
extern "C" void Detach(std::string name) {
  auto i = Bodies.find(name);
  if (i == Bodies.end())
    return;
  b2DestroyBody(i->second.Id);
  Bodies.erase(i);
  for (auto &p : Bodies) {
    auto &c = p.second.Contacts;
    for (auto t : c)
      if (t.Other == name) {
        t.Type = "End";
        p.second.Pending.push_back(t);
      }
    c.erase(std::remove_if(c.begin(), c.end(),
                           [&](const Touch &t) { return t.Other == name; }),
            c.end());
  }
}
extern "C" void Set_Units_Per_Meter(float n) {
  if (!std::isfinite(n) || n <= 0 || !Bodies.empty())
    throw std::runtime_error("Set units before attaching physics bodies");
  Units = n;
}
extern "C" void Set_Box(std::string name, float w, float h) {
  Finite(w, h);
  if (w <= 0 || h <= 0)
    throw std::runtime_error("Box dimensions must be positive");
  auto &b = Find(name);
  auto d = Definition(b);
  auto p = b2MakeBox(w / (2 * Units), h / (2 * Units));
  ClearShape(b);
  b.Shape = b2CreatePolygonShape(b.Id, &d, &p);
}
extern "C" void Set_Circle(std::string name, float r) {
  Finite(r, 0);
  if (r <= 0)
    throw std::runtime_error("Circle radius must be positive");
  auto &b = Find(name);
  auto d = Definition(b);
  b2Circle c = {{0, 0}, r / Units};
  ClearShape(b);
  b.Shape = b2CreateCircleShape(b.Id, &d, &c);
}
extern "C" void Set_Capsule(std::string name, float r, float h) {
  Finite(r, h);
  if (r <= 0 || h < 2 * r)
    throw std::runtime_error("Capsule height must be at least its diameter");
  auto &b = Find(name);
  auto d = Definition(b);
  float y = (h / 2 - r) / Units;
  b2Capsule c = {{0, -y}, {0, y}, r / Units};
  ClearShape(b);
  b.Shape = b2CreateCapsuleShape(b.Id, &d, &c);
}
extern "C" void Set_Body_Type(std::string name, std::string type) {
  b2BodyType t;
  if (type == "Static")
    t = b2_staticBody;
  else if (type == "Dynamic")
    t = b2_dynamicBody;
  else if (type == "Kinematic")
    t = b2_kinematicBody;
  else
    throw std::runtime_error("Unknown body type: " + type);
  b2Body_SetType(Find(name).Id, t);
}
extern "C" void Set_Position(std::string name, float x, float y, float) {
  Finite(x, y);
  auto &b = Find(name);
  b2Body_SetTransform(b.Id, {x / Units, y / Units}, b2Body_GetRotation(b.Id));
  b2Body_SetAwake(b.Id, true);
}
extern "C" void Set_Velocity(std::string name, float x, float y, float) {
  Finite(x, y);
  b2Body_SetLinearVelocity(Find(name).Id, {x / Units, y / Units});
}
extern "C" void Apply_Force(std::string name, float x, float y, float) {
  Finite(x, y);
  b2Body_ApplyForceToCenter(Find(name).Id, {x / Units, y / Units}, true);
}
extern "C" void Apply_Impulse(std::string name, float x, float y, float) {
  Finite(x, y);
  b2Body_ApplyLinearImpulseToCenter(Find(name).Id, {x / Units, y / Units},
                                    true);
}
extern "C" void Set_Gravity(float x, float y, float) {
  Finite(x, y);
  Ensure();
  b2World_SetGravity(World, {x / Units, y / Units});
}
extern "C" void Set(std::string name, std::string key, std::string value) {
  auto &b = Find(name);
  if (key == "Tag") {
    b.Tag = value;
    return;
  }
  if (key == "Category" || key == "Mask") {
    size_t used;
    auto n = std::stoull(value, &used, 0);
    if (used != value.size() || value[0] == '-')
      throw std::runtime_error("Invalid collision filter");
    if (key == "Category")
      b.Category = n;
    else
      b.Mask = n;
    if (b2Shape_IsValid(b.Shape)) {
      auto f = b2Shape_GetFilter(b.Shape);
      f.categoryBits = b.Category;
      f.maskBits = b.Mask;
      b2Shape_SetFilter(b.Shape, f);
    }
    return;
  }
  float n = Number(value);
  if (key == "Sensor") {
    if (b2Shape_IsValid(b.Shape))
      throw std::runtime_error("Set Sensor before creating shape");
    b.Sensor = n != 0;
  } else if (key == "Friction" && n >= 0) {
    b.Friction = n;
    if (b2Shape_IsValid(b.Shape))
      b2Shape_SetFriction(b.Shape, n);
  } else if (key == "Restitution" && n >= 0 && n <= 1) {
    b.Restitution = n;
    if (b2Shape_IsValid(b.Shape))
      b2Shape_SetRestitution(b.Shape, n);
  } else if (key == "Density" && n > 0) {
    b.Density = n;
    if (b2Shape_IsValid(b.Shape))
      b2Shape_SetDensity(b.Shape, n, true);
  } else if (key == "Mass" && n > 0 && b2Shape_IsValid(b.Shape) &&
             b2Body_GetMass(b.Id) > 0) {
    b.Density *= n / b2Body_GetMass(b.Id);
    b2Shape_SetDensity(b.Shape, b.Density, true);
  } else if (key == "Gravity_Scale")
    b2Body_SetGravityScale(b.Id, n);
  else if (key == "Bullet")
    b2Body_SetBullet(b.Id, n != 0);
  else
    throw std::runtime_error("Unsupported physics setting or value: " + key);
}
extern "C" void Step(float seconds) {
  if (!std::isfinite(seconds) || seconds <= 0 || seconds > .1f)
    throw std::runtime_error("Physics Step must be in (0, 0.1] seconds");
  Ensure();
  std::map<std::string, std::vector<Touch>> previous;
  for (auto &p : Bodies) {
    previous[p.first] = p.second.Contacts;
    p.second.Contacts.clear();
    p.second.Events = std::move(p.second.Pending);
    p.second.Pending.clear();
  }
  b2World_Step(World, seconds, 4);
  auto s = b2World_GetSensorEvents(World);
  for (int i = 0; i < s.beginCount; ++i)
    Pair(s.beginEvents[i].sensorShapeId, s.beginEvents[i].visitorShapeId,
         "Begin", true);
  for (int i = 0; i < s.endCount; ++i)
    Pair(s.endEvents[i].sensorShapeId, s.endEvents[i].visitorShapeId, "End",
         true);
  auto e = b2World_GetContactEvents(World);
  for (int i = 0; i < e.beginCount; ++i)
    Pair(e.beginEvents[i].shapeIdA, e.beginEvents[i].shapeIdB, "Begin", false);
  for (int i = 0; i < e.endCount; ++i)
    Pair(e.endEvents[i].shapeIdA, e.endEvents[i].shapeIdB, "End", false);
  for (int i = 0; i < e.hitCount; ++i) {
    auto &h = e.hitEvents[i];
    Pair(h.shapeIdA, h.shapeIdB, "Hit", false, h.normal.x, h.normal.y,
         h.approachSpeed);
  }
  for (auto &p : Bodies) {
    auto &b = p.second;
    std::vector<b2ContactData> data(b2Body_GetContactCapacity(b.Id));
    int count = b2Body_GetContactData(b.Id, data.data(), int(data.size()));
    for (int i = 0; i < count; ++i) {
      auto &c = data[i];
      bool touching = false;
      for (int j = 0; j < c.manifold.pointCount; ++j)
        if (c.manifold.points[j].separation <= .02f)
          touching = true;
      if (!touching)
        continue;
      Body *a = Owner(c.shapeIdA);
      Body *other = Owner(c.shapeIdB);
      float sign = -1;
      if (a != &b) {
        other = a;
        sign = 1;
      }
      if (other)
        Add(b, *other, false, sign * c.manifold.normal.x,
            sign * c.manifold.normal.y);
    }
  }
  // Populate sensors separately so map iteration order cannot erase visitor
  // contacts.
  for (auto &p : Bodies) {
    auto &b = p.second;
    if (!b.Sensor || !b2Shape_IsValid(b.Shape))
      continue;
    std::vector<b2ShapeId> ids(b2Shape_GetSensorCapacity(b.Shape));
    int count = b2Shape_GetSensorData(b.Shape, ids.data(), int(ids.size()));
    for (int i = 0; i < count; ++i)
      if (auto *other = Owner(ids[i])) {
        Add(b, *other, true);
        Add(*other, b, true);
      }
  }
  // Shape replacement/filter changes may invalidate Box2D end-event IDs.
  for (auto &p : previous) {
    auto &b = Find(p.first);
    for (auto t : p.second) {
      bool remains =
          std::any_of(b.Contacts.begin(), b.Contacts.end(),
                      [&](const Touch &c) { return c.Other == t.Other; });
      bool emitted =
          std::any_of(b.Events.begin(), b.Events.end(), [&](const Touch &e) {
            return e.Other == t.Other && e.Type == "End";
          });
      if (!remains && !emitted) {
        t.Type = "End";
        b.Events.push_back(t);
      }
    }
  }
}
extern "C" void Update(std::string) { Step(1.f / 60); }
extern "C" std::string Get(std::string name, std::string key) {
  auto &b = Find(name);
  auto p = b2Body_GetPosition(b.Id);
  auto v = b2Body_GetLinearVelocity(b.Id);
  if (key == "Position_Z" || key == "Velocity_Z")
    return "0";
  if (key == "Position_X")
    return std::to_string(p.x * Units);
  if (key == "Position_Y")
    return std::to_string(p.y * Units);
  if (key == "Velocity_X")
    return std::to_string(v.x * Units);
  if (key == "Velocity_Y")
    return std::to_string(v.y * Units);
  if (key == "Mass")
    return std::to_string(b2Body_GetMass(b.Id));
  if (key == "Tag")
    return b.Tag;
  if (key == "Contact_Count")
    return std::to_string(b.Contacts.size());
  if (key == "Event_Count")
    return std::to_string(b.Events.size());
  if (key == "Grounded") {
    auto g = b2World_GetGravity(World);
    float len = std::hypot(g.x, g.y);
    for (auto &c : b.Contacts)
      if (!c.Sensor && len > 0 && (c.X * g.x + c.Y * g.y) / len < -.5f)
        return "1";
    return "0";
  }
  throw std::runtime_error("Unknown physics field: " + key);
}
namespace {
std::string Field(const Touch &t, const std::string &f) {
  if (f == "Other")
    return t.Other;
  if (f == "Tag")
    return t.Tag;
  if (f == "Type")
    return t.Type;
  if (f == "Sensor")
    return t.Sensor ? "1" : "0";
  if (f == "Normal_Z")
    return "0";
  if (f == "Normal_X")
    return std::to_string(t.X);
  if (f == "Normal_Y")
    return std::to_string(t.Y);
  if (f == "Speed")
    return std::to_string(t.Speed);
  throw std::runtime_error("Unknown contact/event field: " + f);
}
} // namespace
extern "C" std::string Contact(std::string n, int i, std::string f) {
  auto &v = Find(n).Contacts;
  return i < 0 || size_t(i) >= v.size() ? "" : Field(v[i], f);
}
extern "C" std::string Event(std::string n, int i, std::string f) {
  auto &v = Find(n).Events;
  return i < 0 || size_t(i) >= v.size() ? "" : Field(v[i], f);
}
