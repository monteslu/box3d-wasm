// embind glue for Box3D (https://github.com/erincatto/box3d).
// Box3D is Erin Catto's 3D physics engine. This file only adapts its C API
// to JavaScript; all engine credit belongs upstream.
//
// Conventions:
// - Vectors are plain JS objects {x, y, z}; quaternions are {x, y, z, w}.
// - Creation functions take a single options object applied over the Box3D
//   default def, so every field stays optional.
// - World/Body/Shape/Joint wrappers hold ids only. delete() frees the tiny
//   wrapper; destroy() removes the object from the simulation.
// - Bodies and shapes get an auto-assigned integer userData tag so event
//   arrays can be correlated back to application objects.

#include <box3d/box3d.h>
#include <cstdint>
#include <emscripten/bind.h>
#include <emscripten/val.h>
#include <string>
#include <vector>

using emscripten::val;

// ---------------------------------------------------------------------------
// val helpers
// ---------------------------------------------------------------------------

static bool hasKey( const val& o, const char* key )
{
	if ( o.isUndefined() || o.isNull() )
	{
		return false;
	}
	val v = o[key];
	return !v.isUndefined() && !v.isNull();
}

static float getFloat( const val& o, const char* key, float fallback )
{
	return hasKey( o, key ) ? o[key].as<float>() : fallback;
}

static int getInt( const val& o, const char* key, int fallback )
{
	return hasKey( o, key ) ? o[key].as<int>() : fallback;
}

static bool getBool( const val& o, const char* key, bool fallback )
{
	return hasKey( o, key ) ? o[key].as<bool>() : fallback;
}

static uint64_t getU64( const val& o, const char* key, uint64_t fallback )
{
	// JS numbers are safe up to 2^53. Filter bits rarely need more.
	return hasKey( o, key ) ? (uint64_t)o[key].as<double>() : fallback;
}

static b3Vec3 toVec3( const val& v, b3Vec3 fallback )
{
	if ( v.isUndefined() || v.isNull() )
	{
		return fallback;
	}
	b3Vec3 out;
	out.x = hasKey( v, "x" ) ? v["x"].as<float>() : fallback.x;
	out.y = hasKey( v, "y" ) ? v["y"].as<float>() : fallback.y;
	out.z = hasKey( v, "z" ) ? v["z"].as<float>() : fallback.z;
	return out;
}

static b3Vec3 getVec3( const val& o, const char* key, b3Vec3 fallback )
{
	return hasKey( o, key ) ? toVec3( o[key], fallback ) : fallback;
}

static val fromVec3( b3Vec3 v )
{
	val o = val::object();
	o.set( "x", v.x );
	o.set( "y", v.y );
	o.set( "z", v.z );
	return o;
}

static b3Quat toQuat( const val& v, b3Quat fallback )
{
	if ( v.isUndefined() || v.isNull() )
	{
		return fallback;
	}
	b3Quat out;
	out.v.x = hasKey( v, "x" ) ? v["x"].as<float>() : fallback.v.x;
	out.v.y = hasKey( v, "y" ) ? v["y"].as<float>() : fallback.v.y;
	out.v.z = hasKey( v, "z" ) ? v["z"].as<float>() : fallback.v.z;
	out.s = hasKey( v, "w" ) ? v["w"].as<float>() : fallback.s;
	return out;
}

static b3Quat getQuat( const val& o, const char* key, b3Quat fallback )
{
	return hasKey( o, key ) ? toQuat( o[key], fallback ) : fallback;
}

static val fromQuat( b3Quat q )
{
	val o = val::object();
	o.set( "x", q.v.x );
	o.set( "y", q.v.y );
	o.set( "z", q.v.z );
	o.set( "w", q.s );
	return o;
}

static b3Transform getTransform( const val& o, const char* key )
{
	b3Transform xf = b3Transform_identity;
	if ( hasKey( o, key ) )
	{
		val t = o[key];
		xf.p = getVec3( t, "position", b3Vec3_zero );
		xf.q = getQuat( t, "rotation", b3Quat_identity );
	}
	return xf;
}

static val fromTransform( b3Transform xf )
{
	val o = val::object();
	o.set( "position", fromVec3( xf.p ) );
	o.set( "rotation", fromQuat( xf.q ) );
	return o;
}

static double tagOf( void* userData )
{
	return (double)(uintptr_t)userData;
}

// Auto-assigned userData tags. 0 is reserved for "untagged".
static uint32_t g_nextBodyTag = 1;
static uint32_t g_nextShapeTag = 1;

// ---------------------------------------------------------------------------
// Filters and materials
// ---------------------------------------------------------------------------

static b3Filter filterFromOpts( const val& o )
{
	b3Filter filter = b3DefaultFilter();
	if ( hasKey( o, "filter" ) )
	{
		val f = o["filter"];
		filter.categoryBits = getU64( f, "categoryBits", filter.categoryBits );
		filter.maskBits = getU64( f, "maskBits", filter.maskBits );
		filter.groupIndex = getInt( f, "groupIndex", filter.groupIndex );
	}
	return filter;
}

static b3QueryFilter queryFilterFromOpts( const val& o )
{
	b3QueryFilter filter = b3DefaultQueryFilter();
	if ( !o.isUndefined() && !o.isNull() )
	{
		filter.categoryBits = getU64( o, "categoryBits", filter.categoryBits );
		filter.maskBits = getU64( o, "maskBits", filter.maskBits );
	}
	return filter;
}

static b3ShapeDef shapeDefFromOpts( const val& o )
{
	b3ShapeDef def = b3DefaultShapeDef();
	def.userData = (void*)(uintptr_t)( g_nextShapeTag++ );
	if ( o.isUndefined() || o.isNull() )
	{
		return def;
	}
	def.density = getFloat( o, "density", def.density );
	def.baseMaterial.friction = getFloat( o, "friction", def.baseMaterial.friction );
	def.baseMaterial.restitution = getFloat( o, "restitution", def.baseMaterial.restitution );
	def.baseMaterial.rollingResistance = getFloat( o, "rollingResistance", def.baseMaterial.rollingResistance );
	def.baseMaterial.tangentVelocity = getVec3( o, "tangentVelocity", def.baseMaterial.tangentVelocity );
	def.baseMaterial.userMaterialId = getU64( o, "userMaterialId", def.baseMaterial.userMaterialId );
	def.isSensor = getBool( o, "isSensor", def.isSensor );
	def.enableSensorEvents = getBool( o, "enableSensorEvents", def.enableSensorEvents );
	def.enableContactEvents = getBool( o, "enableContactEvents", def.enableContactEvents );
	def.enableHitEvents = getBool( o, "enableHitEvents", def.enableHitEvents );
	def.invokeContactCreation = getBool( o, "invokeContactCreation", def.invokeContactCreation );
	def.updateBodyMass = getBool( o, "updateBodyMass", def.updateBodyMass );
	def.filter = filterFromOpts( o );
	if ( hasKey( o, "userData" ) )
	{
		def.userData = (void*)(uintptr_t)o["userData"].as<double>();
	}
	return def;
}

// ---------------------------------------------------------------------------
// Wrappers
// ---------------------------------------------------------------------------

struct Shape
{
	b3ShapeId id;

	bool isValid() const
	{
		return b3Shape_IsValid( id );
	}

	void destroy( bool updateBodyMass )
	{
		b3DestroyShape( id, updateBodyMass );
		id = b3_nullShapeId;
	}

	std::string getType() const
	{
		switch ( b3Shape_GetType( id ) )
		{
			case b3_sphereShape:
				return "sphere";
			case b3_capsuleShape:
				return "capsule";
			case b3_hullShape:
				return "hull";
			case b3_meshShape:
				return "mesh";
			case b3_heightShape:
				return "heightField";
			case b3_compoundShape:
				return "compound";
			default:
				return "unknown";
		}
	}

	double getUserData() const
	{
		return tagOf( b3Shape_GetUserData( id ) );
	}

	void setUserData( double tag )
	{
		b3Shape_SetUserData( id, (void*)(uintptr_t)tag );
	}

	float getFriction() const
	{
		return b3Shape_GetFriction( id );
	}

	void setFriction( float friction )
	{
		b3Shape_SetFriction( id, friction );
	}

	float getRestitution() const
	{
		return b3Shape_GetRestitution( id );
	}

	void setRestitution( float restitution )
	{
		b3Shape_SetRestitution( id, restitution );
	}

	float getDensity() const
	{
		return b3Shape_GetDensity( id );
	}

	void setDensity( float density, bool updateBodyMass )
	{
		b3Shape_SetDensity( id, density, updateBodyMass );
	}

	bool isSensor() const
	{
		return b3Shape_IsSensor( id );
	}

	void enableSensorEvents( bool flag )
	{
		b3Shape_EnableSensorEvents( id, flag );
	}

	void enableContactEvents( bool flag )
	{
		b3Shape_EnableContactEvents( id, flag );
	}

	void enableHitEvents( bool flag )
	{
		b3Shape_EnableHitEvents( id, flag );
	}

	val getFilter() const
	{
		b3Filter f = b3Shape_GetFilter( id );
		val o = val::object();
		o.set( "categoryBits", (double)f.categoryBits );
		o.set( "maskBits", (double)f.maskBits );
		o.set( "groupIndex", f.groupIndex );
		return o;
	}

	void setFilter( val opts )
	{
		b3Filter f = b3Shape_GetFilter( id );
		f.categoryBits = getU64( opts, "categoryBits", f.categoryBits );
		f.maskBits = getU64( opts, "maskBits", f.maskBits );
		f.groupIndex = getInt( opts, "groupIndex", f.groupIndex );
		b3Shape_SetFilter( id, f, true );
	}

	val getAABB() const
	{
		b3AABB aabb = b3Shape_GetAABB( id );
		val o = val::object();
		o.set( "lowerBound", fromVec3( aabb.lowerBound ) );
		o.set( "upperBound", fromVec3( aabb.upperBound ) );
		return o;
	}

	val rayCast( val origin, val translation ) const
	{
		b3WorldCastOutput out = b3Shape_RayCast( id, toVec3( origin, b3Vec3_zero ), toVec3( translation, b3Vec3_zero ) );
		val o = val::object();
		o.set( "hit", out.hit );
		if ( out.hit )
		{
			o.set( "point", fromVec3( out.point ) );
			o.set( "normal", fromVec3( out.normal ) );
			o.set( "fraction", out.fraction );
		}
		return o;
	}
};

struct Joint
{
	b3JointId id;

	bool isValid() const
	{
		return b3Joint_IsValid( id );
	}

	void destroy( bool wakeAttached )
	{
		b3DestroyJoint( id, wakeAttached );
		id = b3_nullJointId;
	}

	std::string getType() const
	{
		switch ( b3Joint_GetType( id ) )
		{
			case b3_distanceJoint:
				return "distance";
			case b3_filterJoint:
				return "filter";
			case b3_motorJoint:
				return "motor";
			case b3_parallelJoint:
				return "parallel";
			case b3_prismaticJoint:
				return "prismatic";
			case b3_revoluteJoint:
				return "revolute";
			case b3_sphericalJoint:
				return "spherical";
			case b3_weldJoint:
				return "weld";
			case b3_wheelJoint:
				return "wheel";
			default:
				return "unknown";
		}
	}

	void wakeBodies()
	{
		b3Joint_WakeBodies( id );
	}

	bool getCollideConnected() const
	{
		return b3Joint_GetCollideConnected( id );
	}

	void setCollideConnected( bool flag )
	{
		b3Joint_SetCollideConnected( id, flag );
	}

	val getLocalFrameA() const
	{
		return fromTransform( b3Joint_GetLocalFrameA( id ) );
	}

	val getLocalFrameB() const
	{
		return fromTransform( b3Joint_GetLocalFrameB( id ) );
	}

	val getConstraintForce() const
	{
		return fromVec3( b3Joint_GetConstraintForce( id ) );
	}

	val getConstraintTorque() const
	{
		return fromVec3( b3Joint_GetConstraintTorque( id ) );
	}
};

struct DistanceJoint : Joint
{
	void setLength( float length )
	{
		b3DistanceJoint_SetLength( id, length );
	}
	float getLength() const
	{
		return b3DistanceJoint_GetLength( id );
	}
	float getCurrentLength() const
	{
		return b3DistanceJoint_GetCurrentLength( id );
	}
	void enableSpring( bool flag )
	{
		b3DistanceJoint_EnableSpring( id, flag );
	}
	void setSpringHertz( float hertz )
	{
		b3DistanceJoint_SetSpringHertz( id, hertz );
	}
	void setSpringDampingRatio( float ratio )
	{
		b3DistanceJoint_SetSpringDampingRatio( id, ratio );
	}
	void enableLimit( bool flag )
	{
		b3DistanceJoint_EnableLimit( id, flag );
	}
	void setLengthRange( float minLength, float maxLength )
	{
		b3DistanceJoint_SetLengthRange( id, minLength, maxLength );
	}
	void enableMotor( bool flag )
	{
		b3DistanceJoint_EnableMotor( id, flag );
	}
	void setMotorSpeed( float speed )
	{
		b3DistanceJoint_SetMotorSpeed( id, speed );
	}
	void setMaxMotorForce( float force )
	{
		b3DistanceJoint_SetMaxMotorForce( id, force );
	}
};

struct RevoluteJoint : Joint
{
	float getAngle() const
	{
		return b3RevoluteJoint_GetAngle( id );
	}
	void enableSpring( bool flag )
	{
		b3RevoluteJoint_EnableSpring( id, flag );
	}
	void setSpringHertz( float hertz )
	{
		b3RevoluteJoint_SetSpringHertz( id, hertz );
	}
	void setSpringDampingRatio( float ratio )
	{
		b3RevoluteJoint_SetSpringDampingRatio( id, ratio );
	}
	void setTargetAngle( float radians )
	{
		b3RevoluteJoint_SetTargetAngle( id, radians );
	}
	void enableLimit( bool flag )
	{
		b3RevoluteJoint_EnableLimit( id, flag );
	}
	void setLimits( float lower, float upper )
	{
		b3RevoluteJoint_SetLimits( id, lower, upper );
	}
	void enableMotor( bool flag )
	{
		b3RevoluteJoint_EnableMotor( id, flag );
	}
	void setMotorSpeed( float speed )
	{
		b3RevoluteJoint_SetMotorSpeed( id, speed );
	}
	void setMaxMotorTorque( float torque )
	{
		b3RevoluteJoint_SetMaxMotorTorque( id, torque );
	}
	float getMotorTorque() const
	{
		return b3RevoluteJoint_GetMotorTorque( id );
	}
};

struct SphericalJoint : Joint
{
	void enableConeLimit( bool flag )
	{
		b3SphericalJoint_EnableConeLimit( id, flag );
	}
	void setConeLimit( float radians )
	{
		b3SphericalJoint_SetConeLimit( id, radians );
	}
	float getConeAngle() const
	{
		return b3SphericalJoint_GetConeAngle( id );
	}
	void enableTwistLimit( bool flag )
	{
		b3SphericalJoint_EnableTwistLimit( id, flag );
	}
	void setTwistLimits( float lower, float upper )
	{
		b3SphericalJoint_SetTwistLimits( id, lower, upper );
	}
	float getTwistAngle() const
	{
		return b3SphericalJoint_GetTwistAngle( id );
	}
	void enableSpring( bool flag )
	{
		b3SphericalJoint_EnableSpring( id, flag );
	}
	void setSpringHertz( float hertz )
	{
		b3SphericalJoint_SetSpringHertz( id, hertz );
	}
	void setSpringDampingRatio( float ratio )
	{
		b3SphericalJoint_SetSpringDampingRatio( id, ratio );
	}
	void setTargetRotation( val q )
	{
		b3SphericalJoint_SetTargetRotation( id, toQuat( q, b3Quat_identity ) );
	}
	void enableMotor( bool flag )
	{
		b3SphericalJoint_EnableMotor( id, flag );
	}
	void setMotorVelocity( val v )
	{
		b3SphericalJoint_SetMotorVelocity( id, toVec3( v, b3Vec3_zero ) );
	}
	void setMaxMotorTorque( float torque )
	{
		b3SphericalJoint_SetMaxMotorTorque( id, torque );
	}
};

struct PrismaticJoint : Joint
{
	float getTranslation() const
	{
		return b3PrismaticJoint_GetTranslation( id );
	}
	float getSpeed() const
	{
		return b3PrismaticJoint_GetSpeed( id );
	}
	void enableSpring( bool flag )
	{
		b3PrismaticJoint_EnableSpring( id, flag );
	}
	void setSpringHertz( float hertz )
	{
		b3PrismaticJoint_SetSpringHertz( id, hertz );
	}
	void setSpringDampingRatio( float ratio )
	{
		b3PrismaticJoint_SetSpringDampingRatio( id, ratio );
	}
	void setTargetTranslation( float translation )
	{
		b3PrismaticJoint_SetTargetTranslation( id, translation );
	}
	void enableLimit( bool flag )
	{
		b3PrismaticJoint_EnableLimit( id, flag );
	}
	void setLimits( float lower, float upper )
	{
		b3PrismaticJoint_SetLimits( id, lower, upper );
	}
	void enableMotor( bool flag )
	{
		b3PrismaticJoint_EnableMotor( id, flag );
	}
	void setMotorSpeed( float speed )
	{
		b3PrismaticJoint_SetMotorSpeed( id, speed );
	}
	void setMaxMotorForce( float force )
	{
		b3PrismaticJoint_SetMaxMotorForce( id, force );
	}
};

struct WeldJoint : Joint
{
	void setLinearHertz( float hertz )
	{
		b3WeldJoint_SetLinearHertz( id, hertz );
	}
	void setLinearDampingRatio( float ratio )
	{
		b3WeldJoint_SetLinearDampingRatio( id, ratio );
	}
	void setAngularHertz( float hertz )
	{
		b3WeldJoint_SetAngularHertz( id, hertz );
	}
	void setAngularDampingRatio( float ratio )
	{
		b3WeldJoint_SetAngularDampingRatio( id, ratio );
	}
};

struct MotorJoint : Joint
{
	void setLinearVelocity( val v )
	{
		b3MotorJoint_SetLinearVelocity( id, toVec3( v, b3Vec3_zero ) );
	}
	void setAngularVelocity( val v )
	{
		b3MotorJoint_SetAngularVelocity( id, toVec3( v, b3Vec3_zero ) );
	}
	void setMaxVelocityForce( float force )
	{
		b3MotorJoint_SetMaxVelocityForce( id, force );
	}
	void setMaxVelocityTorque( float torque )
	{
		b3MotorJoint_SetMaxVelocityTorque( id, torque );
	}
	void setLinearHertz( float hertz )
	{
		b3MotorJoint_SetLinearHertz( id, hertz );
	}
	void setLinearDampingRatio( float ratio )
	{
		b3MotorJoint_SetLinearDampingRatio( id, ratio );
	}
	void setAngularHertz( float hertz )
	{
		b3MotorJoint_SetAngularHertz( id, hertz );
	}
	void setAngularDampingRatio( float ratio )
	{
		b3MotorJoint_SetAngularDampingRatio( id, ratio );
	}
	void setMaxSpringForce( float force )
	{
		b3MotorJoint_SetMaxSpringForce( id, force );
	}
	void setMaxSpringTorque( float torque )
	{
		b3MotorJoint_SetMaxSpringTorque( id, torque );
	}
};

struct WheelJoint : Joint
{
	void enableSuspension( bool flag )
	{
		b3WheelJoint_EnableSuspension( id, flag );
	}
	void setSuspensionHertz( float hertz )
	{
		b3WheelJoint_SetSuspensionHertz( id, hertz );
	}
	void setSuspensionDampingRatio( float ratio )
	{
		b3WheelJoint_SetSuspensionDampingRatio( id, ratio );
	}
	void enableSuspensionLimit( bool flag )
	{
		b3WheelJoint_EnableSuspensionLimit( id, flag );
	}
	void setSuspensionLimits( float lower, float upper )
	{
		b3WheelJoint_SetSuspensionLimits( id, lower, upper );
	}
	void enableSpinMotor( bool flag )
	{
		b3WheelJoint_EnableSpinMotor( id, flag );
	}
	void setSpinMotorSpeed( float speed )
	{
		b3WheelJoint_SetSpinMotorSpeed( id, speed );
	}
	void setMaxSpinTorque( float torque )
	{
		b3WheelJoint_SetMaxSpinTorque( id, torque );
	}
	float getSpinSpeed() const
	{
		return b3WheelJoint_GetSpinSpeed( id );
	}
	void enableSteering( bool flag )
	{
		b3WheelJoint_EnableSteering( id, flag );
	}
	void setSteeringHertz( float hertz )
	{
		b3WheelJoint_SetSteeringHertz( id, hertz );
	}
	void setSteeringDampingRatio( float ratio )
	{
		b3WheelJoint_SetSteeringDampingRatio( id, ratio );
	}
	void setMaxSteeringTorque( float torque )
	{
		b3WheelJoint_SetMaxSteeringTorque( id, torque );
	}
	void enableSteeringLimit( bool flag )
	{
		b3WheelJoint_EnableSteeringLimit( id, flag );
	}
	void setSteeringLimits( float lower, float upper )
	{
		b3WheelJoint_SetSteeringLimits( id, lower, upper );
	}
	void setTargetSteeringAngle( float radians )
	{
		b3WheelJoint_SetTargetSteeringAngle( id, radians );
	}
	float getSteeringAngle() const
	{
		return b3WheelJoint_GetSteeringAngle( id );
	}
};

struct ParallelJoint : Joint
{
	void setSpringHertz( float hertz )
	{
		b3ParallelJoint_SetSpringHertz( id, hertz );
	}
	void setSpringDampingRatio( float ratio )
	{
		b3ParallelJoint_SetSpringDampingRatio( id, ratio );
	}
	void setMaxTorque( float torque )
	{
		b3ParallelJoint_SetMaxTorque( id, torque );
	}
};

struct FilterJoint : Joint
{
};

struct Body
{
	b3BodyId id;

	bool isValid() const
	{
		return b3Body_IsValid( id );
	}

	void destroy()
	{
		b3DestroyBody( id );
		id = b3_nullBodyId;
	}

	std::string getType() const
	{
		switch ( b3Body_GetType( id ) )
		{
			case b3_staticBody:
				return "static";
			case b3_kinematicBody:
				return "kinematic";
			case b3_dynamicBody:
				return "dynamic";
			default:
				return "unknown";
		}
	}

	void setType( std::string type )
	{
		b3BodyType t = b3_staticBody;
		if ( type == "dynamic" )
		{
			t = b3_dynamicBody;
		}
		else if ( type == "kinematic" )
		{
			t = b3_kinematicBody;
		}
		b3Body_SetType( id, t );
	}

	std::string getName() const
	{
		const char* name = b3Body_GetName( id );
		return name != nullptr ? name : "";
	}

	void setName( std::string name )
	{
		b3Body_SetName( id, name.c_str() );
	}

	double getUserData() const
	{
		return tagOf( b3Body_GetUserData( id ) );
	}

	void setUserData( double tag )
	{
		b3Body_SetUserData( id, (void*)(uintptr_t)tag );
	}

	val getPosition() const
	{
		return fromVec3( b3Body_GetPosition( id ) );
	}

	val getRotation() const
	{
		return fromQuat( b3Body_GetRotation( id ) );
	}

	val getTransform() const
	{
		return fromTransform( b3Body_GetTransform( id ) );
	}

	void setTransform( val position, val rotation )
	{
		b3Body_SetTransform( id, toVec3( position, b3Body_GetPosition( id ) ), toQuat( rotation, b3Body_GetRotation( id ) ) );
	}

	void setTargetTransform( val target, float timeStep, bool wake )
	{
		b3WorldTransform xf;
		xf.p = getVec3( target, "position", b3Body_GetPosition( id ) );
		xf.q = getQuat( target, "rotation", b3Body_GetRotation( id ) );
		b3Body_SetTargetTransform( id, xf, timeStep, wake );
	}

	val getLinearVelocity() const
	{
		return fromVec3( b3Body_GetLinearVelocity( id ) );
	}

	void setLinearVelocity( val v )
	{
		b3Body_SetLinearVelocity( id, toVec3( v, b3Vec3_zero ) );
	}

	val getAngularVelocity() const
	{
		return fromVec3( b3Body_GetAngularVelocity( id ) );
	}

	void setAngularVelocity( val v )
	{
		b3Body_SetAngularVelocity( id, toVec3( v, b3Vec3_zero ) );
	}

	void applyForce( val force, val point, bool wake )
	{
		b3Body_ApplyForce( id, toVec3( force, b3Vec3_zero ), toVec3( point, b3Vec3_zero ), wake );
	}

	void applyForceToCenter( val force, bool wake )
	{
		b3Body_ApplyForceToCenter( id, toVec3( force, b3Vec3_zero ), wake );
	}

	void applyTorque( val torque, bool wake )
	{
		b3Body_ApplyTorque( id, toVec3( torque, b3Vec3_zero ), wake );
	}

	void applyLinearImpulse( val impulse, val point, bool wake )
	{
		b3Body_ApplyLinearImpulse( id, toVec3( impulse, b3Vec3_zero ), toVec3( point, b3Vec3_zero ), wake );
	}

	void applyLinearImpulseToCenter( val impulse, bool wake )
	{
		b3Body_ApplyLinearImpulseToCenter( id, toVec3( impulse, b3Vec3_zero ), wake );
	}

	void applyAngularImpulse( val impulse, bool wake )
	{
		b3Body_ApplyAngularImpulse( id, toVec3( impulse, b3Vec3_zero ), wake );
	}

	float getMass() const
	{
		return b3Body_GetMass( id );
	}

	void applyMassFromShapes()
	{
		b3Body_ApplyMassFromShapes( id );
	}

	val getLocalCenterOfMass() const
	{
		return fromVec3( b3Body_GetLocalCenterOfMass( id ) );
	}

	val getWorldCenterOfMass() const
	{
		return fromVec3( b3Body_GetWorldCenterOfMass( id ) );
	}

	val getLocalPoint( val worldPoint ) const
	{
		return fromVec3( b3Body_GetLocalPoint( id, toVec3( worldPoint, b3Vec3_zero ) ) );
	}

	val getWorldPoint( val localPoint ) const
	{
		return fromVec3( b3Body_GetWorldPoint( id, toVec3( localPoint, b3Vec3_zero ) ) );
	}

	float getLinearDamping() const
	{
		return b3Body_GetLinearDamping( id );
	}

	void setLinearDamping( float damping )
	{
		b3Body_SetLinearDamping( id, damping );
	}

	float getAngularDamping() const
	{
		return b3Body_GetAngularDamping( id );
	}

	void setAngularDamping( float damping )
	{
		b3Body_SetAngularDamping( id, damping );
	}

	float getGravityScale() const
	{
		return b3Body_GetGravityScale( id );
	}

	void setGravityScale( float scale )
	{
		b3Body_SetGravityScale( id, scale );
	}

	bool isAwake() const
	{
		return b3Body_IsAwake( id );
	}

	void setAwake( bool awake )
	{
		b3Body_SetAwake( id, awake );
	}

	void enableSleep( bool flag )
	{
		b3Body_EnableSleep( id, flag );
	}

	bool isEnabled() const
	{
		return b3Body_IsEnabled( id );
	}

	void setEnabled( bool flag )
	{
		if ( flag )
		{
			b3Body_Enable( id );
		}
		else
		{
			b3Body_Disable( id );
		}
	}

	bool isBullet() const
	{
		return b3Body_IsBullet( id );
	}

	void setBullet( bool flag )
	{
		b3Body_SetBullet( id, flag );
	}

	void setMotionLocks( val locks )
	{
		b3MotionLocks ml = b3Body_GetMotionLocks( id );
		ml.linearX = getBool( locks, "linearX", ml.linearX );
		ml.linearY = getBool( locks, "linearY", ml.linearY );
		ml.linearZ = getBool( locks, "linearZ", ml.linearZ );
		ml.angularX = getBool( locks, "angularX", ml.angularX );
		ml.angularY = getBool( locks, "angularY", ml.angularY );
		ml.angularZ = getBool( locks, "angularZ", ml.angularZ );
		b3Body_SetMotionLocks( id, ml );
	}

	val getMotionLocks() const
	{
		b3MotionLocks ml = b3Body_GetMotionLocks( id );
		val o = val::object();
		o.set( "linearX", ml.linearX );
		o.set( "linearY", ml.linearY );
		o.set( "linearZ", ml.linearZ );
		o.set( "angularX", ml.angularX );
		o.set( "angularY", ml.angularY );
		o.set( "angularZ", ml.angularZ );
		return o;
	}

	int getShapeCount() const
	{
		return b3Body_GetShapeCount( id );
	}

	val computeAABB() const
	{
		b3AABB aabb = b3Body_ComputeAABB( id );
		val o = val::object();
		o.set( "lowerBound", fromVec3( aabb.lowerBound ) );
		o.set( "upperBound", fromVec3( aabb.upperBound ) );
		return o;
	}

	// Shape creation. One options object carries both geometry and material.

	Shape createSphere( val opts )
	{
		b3ShapeDef def = shapeDefFromOpts( opts );
		b3Sphere sphere;
		sphere.center = getVec3( opts, "center", b3Vec3_zero );
		sphere.radius = getFloat( opts, "radius", 0.5f );
		Shape shape = { b3CreateSphereShape( id, &def, &sphere ) };
		return shape;
	}

	Shape createCapsule( val opts )
	{
		b3ShapeDef def = shapeDefFromOpts( opts );
		b3Capsule capsule;
		capsule.radius = getFloat( opts, "radius", 0.5f );
		if ( hasKey( opts, "height" ) )
		{
			// height is the segment length between the two hemisphere centers,
			// aligned with the local y axis.
			float half = 0.5f * opts["height"].as<float>();
			capsule.center1 = { 0.0f, -half, 0.0f };
			capsule.center2 = { 0.0f, half, 0.0f };
		}
		else
		{
			b3Vec3 c1 = { 0.0f, -0.5f, 0.0f };
			b3Vec3 c2 = { 0.0f, 0.5f, 0.0f };
			capsule.center1 = getVec3( opts, "center1", c1 );
			capsule.center2 = getVec3( opts, "center2", c2 );
		}
		Shape shape = { b3CreateCapsuleShape( id, &def, &capsule ) };
		return shape;
	}

	Shape createBox( val opts )
	{
		b3ShapeDef def = shapeDefFromOpts( opts );
		b3Vec3 he = { 0.5f, 0.5f, 0.5f };
		if ( hasKey( opts, "halfExtents" ) )
		{
			he = toVec3( opts["halfExtents"], he );
		}
		else if ( hasKey( opts, "hx" ) || hasKey( opts, "hy" ) || hasKey( opts, "hz" ) )
		{
			he.x = getFloat( opts, "hx", 0.5f );
			he.y = getFloat( opts, "hy", 0.5f );
			he.z = getFloat( opts, "hz", 0.5f );
		}

		b3BoxHull box;
		if ( hasKey( opts, "rotation" ) || hasKey( opts, "offset" ) )
		{
			b3Transform xf;
			xf.p = getVec3( opts, "offset", b3Vec3_zero );
			xf.q = getQuat( opts, "rotation", b3Quat_identity );
			box = b3MakeTransformedBoxHull( he.x, he.y, he.z, xf );
		}
		else
		{
			box = b3MakeBoxHull( he.x, he.y, he.z );
		}
		Shape shape = { b3CreateHullShape( id, &def, &box.base ) };
		return shape;
	}

	Shape createHull( val opts )
	{
		b3ShapeDef def = shapeDefFromOpts( opts );
		std::vector<b3Vec3> points;
		val arr = opts["points"];
		int n = arr["length"].as<int>();
		points.reserve( n );
		for ( int i = 0; i < n; ++i )
		{
			points.push_back( toVec3( arr[i], b3Vec3_zero ) );
		}
		int maxVertices = getInt( opts, "maxVertices", 32 );
		b3HullData* hull = b3CreateHull( points.data(), (int)points.size(), maxVertices );
		if ( hull == nullptr )
		{
			Shape shape = { b3_nullShapeId };
			return shape;
		}
		// The world interns hull geometry in its hull database, so the
		// temporary may be freed right after shape creation.
		Shape shape = { b3CreateHullShape( id, &def, hull ) };
		b3DestroyHull( hull );
		return shape;
	}
};

static void applyJointBase( b3JointDef* base, const Body& a, const Body& b, const val& opts )
{
	base->bodyIdA = a.id;
	base->bodyIdB = b.id;
	base->localFrameA = getTransform( opts, "localFrameA" );
	base->localFrameB = getTransform( opts, "localFrameB" );
	if ( hasKey( opts, "anchorA" ) )
	{
		base->localFrameA.p = getVec3( opts, "anchorA", b3Vec3_zero );
	}
	if ( hasKey( opts, "anchorB" ) )
	{
		base->localFrameB.p = getVec3( opts, "anchorB", b3Vec3_zero );
	}
	base->collideConnected = getBool( opts, "collideConnected", base->collideConnected );
	base->forceThreshold = getFloat( opts, "forceThreshold", base->forceThreshold );
	base->torqueThreshold = getFloat( opts, "torqueThreshold", base->torqueThreshold );
}

struct World
{
	b3WorldId id;

	World()
		: World( val::undefined() )
	{
	}

	explicit World( val opts )
	{
		b3WorldDef def = b3DefaultWorldDef();
		if ( !opts.isUndefined() && !opts.isNull() )
		{
			def.gravity = getVec3( opts, "gravity", def.gravity );
			def.restitutionThreshold = getFloat( opts, "restitutionThreshold", def.restitutionThreshold );
			def.hitEventThreshold = getFloat( opts, "hitEventThreshold", def.hitEventThreshold );
			def.contactHertz = getFloat( opts, "contactHertz", def.contactHertz );
			def.contactDampingRatio = getFloat( opts, "contactDampingRatio", def.contactDampingRatio );
			def.contactSpeed = getFloat( opts, "contactSpeed", def.contactSpeed );
			def.maximumLinearSpeed = getFloat( opts, "maximumLinearSpeed", def.maximumLinearSpeed );
			def.enableSleep = getBool( opts, "enableSleep", def.enableSleep );
			def.enableContinuous = getBool( opts, "enableContinuous", def.enableContinuous );
#ifdef __EMSCRIPTEN_PTHREADS__
			int workers = getInt( opts, "workerCount", 1 );
			if ( workers < 1 )
			{
				workers = 1;
			}
			if ( workers > B3_MAX_WORKERS )
			{
				workers = B3_MAX_WORKERS;
			}
			def.workerCount = (uint32_t)workers;
#endif
		}
		id = b3CreateWorld( &def );
	}

	bool isValid() const
	{
		return b3World_IsValid( id );
	}

	void destroy()
	{
		b3DestroyWorld( id );
		id = b3_nullWorldId;
	}

	void step( float timeStep, int subStepCount )
	{
		b3World_Step( id, timeStep, subStepCount );
	}

	val getGravity() const
	{
		return fromVec3( b3World_GetGravity( id ) );
	}

	void setGravity( val gravity )
	{
		b3World_SetGravity( id, toVec3( gravity, b3Vec3_zero ) );
	}

	void enableSleeping( bool flag )
	{
		b3World_EnableSleeping( id, flag );
	}

	void enableContinuous( bool flag )
	{
		b3World_EnableContinuous( id, flag );
	}

	int getAwakeBodyCount() const
	{
		return b3World_GetAwakeBodyCount( id );
	}

	int getWorkerCount() const
	{
		return b3World_GetWorkerCount( id );
	}

	Body createBody( val opts )
	{
		b3BodyDef def = b3DefaultBodyDef();
		def.userData = (void*)(uintptr_t)( g_nextBodyTag++ );
		std::string name;
		if ( !opts.isUndefined() && !opts.isNull() )
		{
			if ( hasKey( opts, "type" ) )
			{
				std::string type = opts["type"].as<std::string>();
				if ( type == "dynamic" )
				{
					def.type = b3_dynamicBody;
				}
				else if ( type == "kinematic" )
				{
					def.type = b3_kinematicBody;
				}
				else
				{
					def.type = b3_staticBody;
				}
			}
			def.position = getVec3( opts, "position", def.position );
			def.rotation = getQuat( opts, "rotation", def.rotation );
			def.linearVelocity = getVec3( opts, "linearVelocity", def.linearVelocity );
			def.angularVelocity = getVec3( opts, "angularVelocity", def.angularVelocity );
			def.linearDamping = getFloat( opts, "linearDamping", def.linearDamping );
			def.angularDamping = getFloat( opts, "angularDamping", def.angularDamping );
			def.gravityScale = getFloat( opts, "gravityScale", def.gravityScale );
			def.sleepThreshold = getFloat( opts, "sleepThreshold", def.sleepThreshold );
			def.enableSleep = getBool( opts, "enableSleep", def.enableSleep );
			def.isAwake = getBool( opts, "isAwake", def.isAwake );
			def.isBullet = getBool( opts, "isBullet", def.isBullet );
			def.isEnabled = getBool( opts, "isEnabled", def.isEnabled );
			def.allowFastRotation = getBool( opts, "allowFastRotation", def.allowFastRotation );
			def.enableContactRecycling = getBool( opts, "enableContactRecycling", def.enableContactRecycling );
			if ( hasKey( opts, "motionLocks" ) )
			{
				val locks = opts["motionLocks"];
				def.motionLocks.linearX = getBool( locks, "linearX", false );
				def.motionLocks.linearY = getBool( locks, "linearY", false );
				def.motionLocks.linearZ = getBool( locks, "linearZ", false );
				def.motionLocks.angularX = getBool( locks, "angularX", false );
				def.motionLocks.angularY = getBool( locks, "angularY", false );
				def.motionLocks.angularZ = getBool( locks, "angularZ", false );
			}
			if ( hasKey( opts, "userData" ) )
			{
				def.userData = (void*)(uintptr_t)opts["userData"].as<double>();
			}
			if ( hasKey( opts, "name" ) )
			{
				name = opts["name"].as<std::string>();
				def.name = name.c_str();
			}
		}
		Body body = { b3CreateBody( id, &def ) };
		return body;
	}

	// Joints

	DistanceJoint createDistanceJoint( const Body& a, const Body& b, val opts )
	{
		b3DistanceJointDef def = b3DefaultDistanceJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.length = getFloat( opts, "length", def.length );
		def.enableSpring = getBool( opts, "enableSpring", def.enableSpring );
		def.hertz = getFloat( opts, "hertz", def.hertz );
		def.dampingRatio = getFloat( opts, "dampingRatio", def.dampingRatio );
		def.lowerSpringForce = getFloat( opts, "lowerSpringForce", def.lowerSpringForce );
		def.upperSpringForce = getFloat( opts, "upperSpringForce", def.upperSpringForce );
		def.enableLimit = getBool( opts, "enableLimit", def.enableLimit );
		def.minLength = getFloat( opts, "minLength", def.minLength );
		def.maxLength = getFloat( opts, "maxLength", def.maxLength );
		def.enableMotor = getBool( opts, "enableMotor", def.enableMotor );
		def.maxMotorForce = getFloat( opts, "maxMotorForce", def.maxMotorForce );
		def.motorSpeed = getFloat( opts, "motorSpeed", def.motorSpeed );
		DistanceJoint joint;
		joint.id = b3CreateDistanceJoint( id, &def );
		return joint;
	}

	RevoluteJoint createRevoluteJoint( const Body& a, const Body& b, val opts )
	{
		b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.targetAngle = getFloat( opts, "targetAngle", def.targetAngle );
		def.enableSpring = getBool( opts, "enableSpring", def.enableSpring );
		def.hertz = getFloat( opts, "hertz", def.hertz );
		def.dampingRatio = getFloat( opts, "dampingRatio", def.dampingRatio );
		def.enableLimit = getBool( opts, "enableLimit", def.enableLimit );
		def.lowerAngle = getFloat( opts, "lowerAngle", def.lowerAngle );
		def.upperAngle = getFloat( opts, "upperAngle", def.upperAngle );
		def.enableMotor = getBool( opts, "enableMotor", def.enableMotor );
		def.maxMotorTorque = getFloat( opts, "maxMotorTorque", def.maxMotorTorque );
		def.motorSpeed = getFloat( opts, "motorSpeed", def.motorSpeed );
		RevoluteJoint joint;
		joint.id = b3CreateRevoluteJoint( id, &def );
		return joint;
	}

	SphericalJoint createSphericalJoint( const Body& a, const Body& b, val opts )
	{
		b3SphericalJointDef def = b3DefaultSphericalJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.enableSpring = getBool( opts, "enableSpring", def.enableSpring );
		def.hertz = getFloat( opts, "hertz", def.hertz );
		def.dampingRatio = getFloat( opts, "dampingRatio", def.dampingRatio );
		def.targetRotation = getQuat( opts, "targetRotation", def.targetRotation );
		def.enableConeLimit = getBool( opts, "enableConeLimit", def.enableConeLimit );
		def.coneAngle = getFloat( opts, "coneAngle", def.coneAngle );
		def.enableTwistLimit = getBool( opts, "enableTwistLimit", def.enableTwistLimit );
		def.lowerTwistAngle = getFloat( opts, "lowerTwistAngle", def.lowerTwistAngle );
		def.upperTwistAngle = getFloat( opts, "upperTwistAngle", def.upperTwistAngle );
		def.enableMotor = getBool( opts, "enableMotor", def.enableMotor );
		def.maxMotorTorque = getFloat( opts, "maxMotorTorque", def.maxMotorTorque );
		def.motorVelocity = getVec3( opts, "motorVelocity", def.motorVelocity );
		SphericalJoint joint;
		joint.id = b3CreateSphericalJoint( id, &def );
		return joint;
	}

	PrismaticJoint createPrismaticJoint( const Body& a, const Body& b, val opts )
	{
		b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.enableSpring = getBool( opts, "enableSpring", def.enableSpring );
		def.hertz = getFloat( opts, "hertz", def.hertz );
		def.dampingRatio = getFloat( opts, "dampingRatio", def.dampingRatio );
		def.targetTranslation = getFloat( opts, "targetTranslation", def.targetTranslation );
		def.enableLimit = getBool( opts, "enableLimit", def.enableLimit );
		def.lowerTranslation = getFloat( opts, "lowerTranslation", def.lowerTranslation );
		def.upperTranslation = getFloat( opts, "upperTranslation", def.upperTranslation );
		def.enableMotor = getBool( opts, "enableMotor", def.enableMotor );
		def.maxMotorForce = getFloat( opts, "maxMotorForce", def.maxMotorForce );
		def.motorSpeed = getFloat( opts, "motorSpeed", def.motorSpeed );
		PrismaticJoint joint;
		joint.id = b3CreatePrismaticJoint( id, &def );
		return joint;
	}

	WeldJoint createWeldJoint( const Body& a, const Body& b, val opts )
	{
		b3WeldJointDef def = b3DefaultWeldJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.linearHertz = getFloat( opts, "linearHertz", def.linearHertz );
		def.angularHertz = getFloat( opts, "angularHertz", def.angularHertz );
		def.linearDampingRatio = getFloat( opts, "linearDampingRatio", def.linearDampingRatio );
		def.angularDampingRatio = getFloat( opts, "angularDampingRatio", def.angularDampingRatio );
		WeldJoint joint;
		joint.id = b3CreateWeldJoint( id, &def );
		return joint;
	}

	MotorJoint createMotorJoint( const Body& a, const Body& b, val opts )
	{
		b3MotorJointDef def = b3DefaultMotorJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.linearVelocity = getVec3( opts, "linearVelocity", def.linearVelocity );
		def.maxVelocityForce = getFloat( opts, "maxVelocityForce", def.maxVelocityForce );
		def.angularVelocity = getVec3( opts, "angularVelocity", def.angularVelocity );
		def.maxVelocityTorque = getFloat( opts, "maxVelocityTorque", def.maxVelocityTorque );
		def.linearHertz = getFloat( opts, "linearHertz", def.linearHertz );
		def.linearDampingRatio = getFloat( opts, "linearDampingRatio", def.linearDampingRatio );
		def.maxSpringForce = getFloat( opts, "maxSpringForce", def.maxSpringForce );
		def.angularHertz = getFloat( opts, "angularHertz", def.angularHertz );
		def.angularDampingRatio = getFloat( opts, "angularDampingRatio", def.angularDampingRatio );
		def.maxSpringTorque = getFloat( opts, "maxSpringTorque", def.maxSpringTorque );
		MotorJoint joint;
		joint.id = b3CreateMotorJoint( id, &def );
		return joint;
	}

	WheelJoint createWheelJoint( const Body& a, const Body& b, val opts )
	{
		b3WheelJointDef def = b3DefaultWheelJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.enableSuspensionSpring = getBool( opts, "enableSuspensionSpring", def.enableSuspensionSpring );
		def.suspensionHertz = getFloat( opts, "suspensionHertz", def.suspensionHertz );
		def.suspensionDampingRatio = getFloat( opts, "suspensionDampingRatio", def.suspensionDampingRatio );
		def.enableSuspensionLimit = getBool( opts, "enableSuspensionLimit", def.enableSuspensionLimit );
		def.lowerSuspensionLimit = getFloat( opts, "lowerSuspensionLimit", def.lowerSuspensionLimit );
		def.upperSuspensionLimit = getFloat( opts, "upperSuspensionLimit", def.upperSuspensionLimit );
		def.enableSpinMotor = getBool( opts, "enableSpinMotor", def.enableSpinMotor );
		def.maxSpinTorque = getFloat( opts, "maxSpinTorque", def.maxSpinTorque );
		def.spinSpeed = getFloat( opts, "spinSpeed", def.spinSpeed );
		def.enableSteering = getBool( opts, "enableSteering", def.enableSteering );
		def.steeringHertz = getFloat( opts, "steeringHertz", def.steeringHertz );
		def.steeringDampingRatio = getFloat( opts, "steeringDampingRatio", def.steeringDampingRatio );
		def.targetSteeringAngle = getFloat( opts, "targetSteeringAngle", def.targetSteeringAngle );
		def.maxSteeringTorque = getFloat( opts, "maxSteeringTorque", def.maxSteeringTorque );
		def.enableSteeringLimit = getBool( opts, "enableSteeringLimit", def.enableSteeringLimit );
		def.lowerSteeringLimit = getFloat( opts, "lowerSteeringLimit", def.lowerSteeringLimit );
		def.upperSteeringLimit = getFloat( opts, "upperSteeringLimit", def.upperSteeringLimit );
		WheelJoint joint;
		joint.id = b3CreateWheelJoint( id, &def );
		return joint;
	}

	ParallelJoint createParallelJoint( const Body& a, const Body& b, val opts )
	{
		b3ParallelJointDef def = b3DefaultParallelJointDef();
		applyJointBase( &def.base, a, b, opts );
		def.hertz = getFloat( opts, "hertz", def.hertz );
		def.dampingRatio = getFloat( opts, "dampingRatio", def.dampingRatio );
		def.maxTorque = getFloat( opts, "maxTorque", def.maxTorque );
		ParallelJoint joint;
		joint.id = b3CreateParallelJoint( id, &def );
		return joint;
	}

	FilterJoint createFilterJoint( const Body& a, const Body& b, val opts )
	{
		b3FilterJointDef def = b3DefaultFilterJointDef();
		applyJointBase( &def.base, a, b, opts );
		FilterJoint joint;
		joint.id = b3CreateFilterJoint( id, &def );
		return joint;
	}

	// Queries

	val castRayClosest( val origin, val translation, val filterOpts ) const
	{
		b3RayResult result = b3World_CastRayClosest( id, toVec3( origin, b3Vec3_zero ), toVec3( translation, b3Vec3_zero ),
													 queryFilterFromOpts( filterOpts ) );
		val o = val::object();
		o.set( "hit", result.hit );
		if ( result.hit )
		{
			o.set( "point", fromVec3( result.point ) );
			o.set( "normal", fromVec3( result.normal ) );
			o.set( "fraction", result.fraction );
			o.set( "shapeUserData", tagOf( b3Shape_GetUserData( result.shapeId ) ) );
			o.set( "bodyUserData", tagOf( b3Body_GetUserData( b3Shape_GetBody( result.shapeId ) ) ) );
			Shape shape = { result.shapeId };
			o.set( "shape", val( shape ) );
		}
		return o;
	}

	void explode( val opts )
	{
		b3ExplosionDef def = b3DefaultExplosionDef();
		def.position = getVec3( opts, "position", def.position );
		def.radius = getFloat( opts, "radius", def.radius );
		def.falloff = getFloat( opts, "falloff", def.falloff );
		def.impulsePerArea = getFloat( opts, "impulsePerArea", def.impulsePerArea );
		def.maskBits = getU64( opts, "maskBits", def.maskBits );
		b3World_Explode( id, &def );
	}

	// Events. Arrays of plain objects, correlate with userData tags.

	val getBodyEvents() const
	{
		b3BodyEvents events = b3World_GetBodyEvents( id );
		val arr = val::array();
		for ( int i = 0; i < events.moveCount; ++i )
		{
			const b3BodyMoveEvent& e = events.moveEvents[i];
			val o = val::object();
			o.set( "userData", tagOf( e.userData ) );
			o.set( "position", fromVec3( e.transform.p ) );
			o.set( "rotation", fromQuat( e.transform.q ) );
			o.set( "fellAsleep", e.fellAsleep );
			arr.set( i, o );
		}
		return arr;
	}

	val getContactEvents() const
	{
		b3ContactEvents events = b3World_GetContactEvents( id );
		val out = val::object();

		val begin = val::array();
		for ( int i = 0; i < events.beginCount; ++i )
		{
			const b3ContactBeginTouchEvent& e = events.beginEvents[i];
			val o = val::object();
			o.set( "shapeUserDataA", tagOf( b3Shape_GetUserData( e.shapeIdA ) ) );
			o.set( "shapeUserDataB", tagOf( b3Shape_GetUserData( e.shapeIdB ) ) );
			begin.set( i, o );
		}
		out.set( "begin", begin );

		val end = val::array();
		for ( int i = 0; i < events.endCount; ++i )
		{
			const b3ContactEndTouchEvent& e = events.endEvents[i];
			val o = val::object();
			bool validA = b3Shape_IsValid( e.shapeIdA );
			bool validB = b3Shape_IsValid( e.shapeIdB );
			o.set( "shapeUserDataA", validA ? val( tagOf( b3Shape_GetUserData( e.shapeIdA ) ) ) : val::null() );
			o.set( "shapeUserDataB", validB ? val( tagOf( b3Shape_GetUserData( e.shapeIdB ) ) ) : val::null() );
			end.set( i, o );
		}
		out.set( "end", end );

		val hit = val::array();
		for ( int i = 0; i < events.hitCount; ++i )
		{
			const b3ContactHitEvent& e = events.hitEvents[i];
			val o = val::object();
			o.set( "shapeUserDataA", tagOf( b3Shape_GetUserData( e.shapeIdA ) ) );
			o.set( "shapeUserDataB", tagOf( b3Shape_GetUserData( e.shapeIdB ) ) );
			o.set( "point", fromVec3( e.point ) );
			o.set( "normal", fromVec3( e.normal ) );
			o.set( "approachSpeed", e.approachSpeed );
			hit.set( i, o );
		}
		out.set( "hit", hit );

		return out;
	}

	val getSensorEvents() const
	{
		b3SensorEvents events = b3World_GetSensorEvents( id );
		val out = val::object();

		val begin = val::array();
		for ( int i = 0; i < events.beginCount; ++i )
		{
			val o = val::object();
			o.set( "sensorUserData", tagOf( b3Shape_GetUserData( events.beginEvents[i].sensorShapeId ) ) );
			o.set( "visitorUserData", tagOf( b3Shape_GetUserData( events.beginEvents[i].visitorShapeId ) ) );
			begin.set( i, o );
		}
		out.set( "begin", begin );

		val end = val::array();
		for ( int i = 0; i < events.endCount; ++i )
		{
			val o = val::object();
			bool validSensor = b3Shape_IsValid( events.endEvents[i].sensorShapeId );
			bool validVisitor = b3Shape_IsValid( events.endEvents[i].visitorShapeId );
			o.set( "sensorUserData",
				   validSensor ? val( tagOf( b3Shape_GetUserData( events.endEvents[i].sensorShapeId ) ) ) : val::null() );
			o.set( "visitorUserData",
				   validVisitor ? val( tagOf( b3Shape_GetUserData( events.endEvents[i].visitorShapeId ) ) ) : val::null() );
			end.set( i, o );
		}
		out.set( "end", end );

		return out;
	}

	val getProfile() const
	{
		b3Profile p = b3World_GetProfile( id );
		val o = val::object();
		o.set( "step", p.step );
		o.set( "pairs", p.pairs );
		o.set( "collide", p.collide );
		o.set( "solve", p.solve );
		return o;
	}
};

// ---------------------------------------------------------------------------
// Bindings
// ---------------------------------------------------------------------------

EMSCRIPTEN_BINDINGS( box3d )
{
	using namespace emscripten;

#ifdef __EMSCRIPTEN_PTHREADS__
	constant( "threaded", true );
#else
	constant( "threaded", false );
#endif
	constant( "maxWorkers", (int)B3_MAX_WORKERS );

	class_<World>( "World" )
		.constructor<>()
		.constructor<val>()
		.function( "isValid", &World::isValid )
		.function( "destroy", &World::destroy )
		.function( "step", &World::step )
		.function( "getGravity", &World::getGravity )
		.function( "setGravity", &World::setGravity )
		.function( "enableSleeping", &World::enableSleeping )
		.function( "enableContinuous", &World::enableContinuous )
		.function( "getAwakeBodyCount", &World::getAwakeBodyCount )
		.function( "getWorkerCount", &World::getWorkerCount )
		.function( "createBody", &World::createBody )
		.function( "createDistanceJoint", &World::createDistanceJoint )
		.function( "createRevoluteJoint", &World::createRevoluteJoint )
		.function( "createSphericalJoint", &World::createSphericalJoint )
		.function( "createPrismaticJoint", &World::createPrismaticJoint )
		.function( "createWeldJoint", &World::createWeldJoint )
		.function( "createMotorJoint", &World::createMotorJoint )
		.function( "createWheelJoint", &World::createWheelJoint )
		.function( "createParallelJoint", &World::createParallelJoint )
		.function( "createFilterJoint", &World::createFilterJoint )
		.function( "castRayClosest", &World::castRayClosest )
		.function( "explode", &World::explode )
		.function( "getBodyEvents", &World::getBodyEvents )
		.function( "getContactEvents", &World::getContactEvents )
		.function( "getSensorEvents", &World::getSensorEvents )
		.function( "getProfile", &World::getProfile );

	class_<Body>( "Body" )
		.function( "isValid", &Body::isValid )
		.function( "destroy", &Body::destroy )
		.function( "getType", &Body::getType )
		.function( "setType", &Body::setType )
		.function( "getName", &Body::getName )
		.function( "setName", &Body::setName )
		.function( "getUserData", &Body::getUserData )
		.function( "setUserData", &Body::setUserData )
		.function( "getPosition", &Body::getPosition )
		.function( "getRotation", &Body::getRotation )
		.function( "getTransform", &Body::getTransform )
		.function( "setTransform", &Body::setTransform )
		.function( "setTargetTransform", &Body::setTargetTransform )
		.function( "getLinearVelocity", &Body::getLinearVelocity )
		.function( "setLinearVelocity", &Body::setLinearVelocity )
		.function( "getAngularVelocity", &Body::getAngularVelocity )
		.function( "setAngularVelocity", &Body::setAngularVelocity )
		.function( "applyForce", &Body::applyForce )
		.function( "applyForceToCenter", &Body::applyForceToCenter )
		.function( "applyTorque", &Body::applyTorque )
		.function( "applyLinearImpulse", &Body::applyLinearImpulse )
		.function( "applyLinearImpulseToCenter", &Body::applyLinearImpulseToCenter )
		.function( "applyAngularImpulse", &Body::applyAngularImpulse )
		.function( "getMass", &Body::getMass )
		.function( "applyMassFromShapes", &Body::applyMassFromShapes )
		.function( "getLocalCenterOfMass", &Body::getLocalCenterOfMass )
		.function( "getWorldCenterOfMass", &Body::getWorldCenterOfMass )
		.function( "getLocalPoint", &Body::getLocalPoint )
		.function( "getWorldPoint", &Body::getWorldPoint )
		.function( "getLinearDamping", &Body::getLinearDamping )
		.function( "setLinearDamping", &Body::setLinearDamping )
		.function( "getAngularDamping", &Body::getAngularDamping )
		.function( "setAngularDamping", &Body::setAngularDamping )
		.function( "getGravityScale", &Body::getGravityScale )
		.function( "setGravityScale", &Body::setGravityScale )
		.function( "isAwake", &Body::isAwake )
		.function( "setAwake", &Body::setAwake )
		.function( "enableSleep", &Body::enableSleep )
		.function( "isEnabled", &Body::isEnabled )
		.function( "setEnabled", &Body::setEnabled )
		.function( "isBullet", &Body::isBullet )
		.function( "setBullet", &Body::setBullet )
		.function( "setMotionLocks", &Body::setMotionLocks )
		.function( "getMotionLocks", &Body::getMotionLocks )
		.function( "getShapeCount", &Body::getShapeCount )
		.function( "computeAABB", &Body::computeAABB )
		.function( "createSphere", &Body::createSphere )
		.function( "createCapsule", &Body::createCapsule )
		.function( "createBox", &Body::createBox )
		.function( "createHull", &Body::createHull );

	class_<Shape>( "Shape" )
		.function( "isValid", &Shape::isValid )
		.function( "destroy", &Shape::destroy )
		.function( "getType", &Shape::getType )
		.function( "getUserData", &Shape::getUserData )
		.function( "setUserData", &Shape::setUserData )
		.function( "getFriction", &Shape::getFriction )
		.function( "setFriction", &Shape::setFriction )
		.function( "getRestitution", &Shape::getRestitution )
		.function( "setRestitution", &Shape::setRestitution )
		.function( "getDensity", &Shape::getDensity )
		.function( "setDensity", &Shape::setDensity )
		.function( "isSensor", &Shape::isSensor )
		.function( "enableSensorEvents", &Shape::enableSensorEvents )
		.function( "enableContactEvents", &Shape::enableContactEvents )
		.function( "enableHitEvents", &Shape::enableHitEvents )
		.function( "getFilter", &Shape::getFilter )
		.function( "setFilter", &Shape::setFilter )
		.function( "getAABB", &Shape::getAABB )
		.function( "rayCast", &Shape::rayCast );

	class_<Joint>( "Joint" )
		.function( "isValid", &Joint::isValid )
		.function( "destroy", &Joint::destroy )
		.function( "getType", &Joint::getType )
		.function( "wakeBodies", &Joint::wakeBodies )
		.function( "getCollideConnected", &Joint::getCollideConnected )
		.function( "setCollideConnected", &Joint::setCollideConnected )
		.function( "getLocalFrameA", &Joint::getLocalFrameA )
		.function( "getLocalFrameB", &Joint::getLocalFrameB )
		.function( "getConstraintForce", &Joint::getConstraintForce )
		.function( "getConstraintTorque", &Joint::getConstraintTorque );

	class_<DistanceJoint, base<Joint>>( "DistanceJoint" )
		.function( "setLength", &DistanceJoint::setLength )
		.function( "getLength", &DistanceJoint::getLength )
		.function( "getCurrentLength", &DistanceJoint::getCurrentLength )
		.function( "enableSpring", &DistanceJoint::enableSpring )
		.function( "setSpringHertz", &DistanceJoint::setSpringHertz )
		.function( "setSpringDampingRatio", &DistanceJoint::setSpringDampingRatio )
		.function( "enableLimit", &DistanceJoint::enableLimit )
		.function( "setLengthRange", &DistanceJoint::setLengthRange )
		.function( "enableMotor", &DistanceJoint::enableMotor )
		.function( "setMotorSpeed", &DistanceJoint::setMotorSpeed )
		.function( "setMaxMotorForce", &DistanceJoint::setMaxMotorForce );

	class_<RevoluteJoint, base<Joint>>( "RevoluteJoint" )
		.function( "getAngle", &RevoluteJoint::getAngle )
		.function( "enableSpring", &RevoluteJoint::enableSpring )
		.function( "setSpringHertz", &RevoluteJoint::setSpringHertz )
		.function( "setSpringDampingRatio", &RevoluteJoint::setSpringDampingRatio )
		.function( "setTargetAngle", &RevoluteJoint::setTargetAngle )
		.function( "enableLimit", &RevoluteJoint::enableLimit )
		.function( "setLimits", &RevoluteJoint::setLimits )
		.function( "enableMotor", &RevoluteJoint::enableMotor )
		.function( "setMotorSpeed", &RevoluteJoint::setMotorSpeed )
		.function( "setMaxMotorTorque", &RevoluteJoint::setMaxMotorTorque )
		.function( "getMotorTorque", &RevoluteJoint::getMotorTorque );

	class_<SphericalJoint, base<Joint>>( "SphericalJoint" )
		.function( "enableConeLimit", &SphericalJoint::enableConeLimit )
		.function( "setConeLimit", &SphericalJoint::setConeLimit )
		.function( "getConeAngle", &SphericalJoint::getConeAngle )
		.function( "enableTwistLimit", &SphericalJoint::enableTwistLimit )
		.function( "setTwistLimits", &SphericalJoint::setTwistLimits )
		.function( "getTwistAngle", &SphericalJoint::getTwistAngle )
		.function( "enableSpring", &SphericalJoint::enableSpring )
		.function( "setSpringHertz", &SphericalJoint::setSpringHertz )
		.function( "setSpringDampingRatio", &SphericalJoint::setSpringDampingRatio )
		.function( "setTargetRotation", &SphericalJoint::setTargetRotation )
		.function( "enableMotor", &SphericalJoint::enableMotor )
		.function( "setMotorVelocity", &SphericalJoint::setMotorVelocity )
		.function( "setMaxMotorTorque", &SphericalJoint::setMaxMotorTorque );

	class_<PrismaticJoint, base<Joint>>( "PrismaticJoint" )
		.function( "getTranslation", &PrismaticJoint::getTranslation )
		.function( "getSpeed", &PrismaticJoint::getSpeed )
		.function( "enableSpring", &PrismaticJoint::enableSpring )
		.function( "setSpringHertz", &PrismaticJoint::setSpringHertz )
		.function( "setSpringDampingRatio", &PrismaticJoint::setSpringDampingRatio )
		.function( "setTargetTranslation", &PrismaticJoint::setTargetTranslation )
		.function( "enableLimit", &PrismaticJoint::enableLimit )
		.function( "setLimits", &PrismaticJoint::setLimits )
		.function( "enableMotor", &PrismaticJoint::enableMotor )
		.function( "setMotorSpeed", &PrismaticJoint::setMotorSpeed )
		.function( "setMaxMotorForce", &PrismaticJoint::setMaxMotorForce );

	class_<WeldJoint, base<Joint>>( "WeldJoint" )
		.function( "setLinearHertz", &WeldJoint::setLinearHertz )
		.function( "setLinearDampingRatio", &WeldJoint::setLinearDampingRatio )
		.function( "setAngularHertz", &WeldJoint::setAngularHertz )
		.function( "setAngularDampingRatio", &WeldJoint::setAngularDampingRatio );

	class_<MotorJoint, base<Joint>>( "MotorJoint" )
		.function( "setLinearVelocity", &MotorJoint::setLinearVelocity )
		.function( "setAngularVelocity", &MotorJoint::setAngularVelocity )
		.function( "setMaxVelocityForce", &MotorJoint::setMaxVelocityForce )
		.function( "setMaxVelocityTorque", &MotorJoint::setMaxVelocityTorque )
		.function( "setLinearHertz", &MotorJoint::setLinearHertz )
		.function( "setLinearDampingRatio", &MotorJoint::setLinearDampingRatio )
		.function( "setAngularHertz", &MotorJoint::setAngularHertz )
		.function( "setAngularDampingRatio", &MotorJoint::setAngularDampingRatio )
		.function( "setMaxSpringForce", &MotorJoint::setMaxSpringForce )
		.function( "setMaxSpringTorque", &MotorJoint::setMaxSpringTorque );

	class_<WheelJoint, base<Joint>>( "WheelJoint" )
		.function( "enableSuspension", &WheelJoint::enableSuspension )
		.function( "setSuspensionHertz", &WheelJoint::setSuspensionHertz )
		.function( "setSuspensionDampingRatio", &WheelJoint::setSuspensionDampingRatio )
		.function( "enableSuspensionLimit", &WheelJoint::enableSuspensionLimit )
		.function( "setSuspensionLimits", &WheelJoint::setSuspensionLimits )
		.function( "enableSpinMotor", &WheelJoint::enableSpinMotor )
		.function( "setSpinMotorSpeed", &WheelJoint::setSpinMotorSpeed )
		.function( "setMaxSpinTorque", &WheelJoint::setMaxSpinTorque )
		.function( "getSpinSpeed", &WheelJoint::getSpinSpeed )
		.function( "enableSteering", &WheelJoint::enableSteering )
		.function( "setSteeringHertz", &WheelJoint::setSteeringHertz )
		.function( "setSteeringDampingRatio", &WheelJoint::setSteeringDampingRatio )
		.function( "setMaxSteeringTorque", &WheelJoint::setMaxSteeringTorque )
		.function( "enableSteeringLimit", &WheelJoint::enableSteeringLimit )
		.function( "setSteeringLimits", &WheelJoint::setSteeringLimits )
		.function( "setTargetSteeringAngle", &WheelJoint::setTargetSteeringAngle )
		.function( "getSteeringAngle", &WheelJoint::getSteeringAngle );

	class_<ParallelJoint, base<Joint>>( "ParallelJoint" )
		.function( "setSpringHertz", &ParallelJoint::setSpringHertz )
		.function( "setSpringDampingRatio", &ParallelJoint::setSpringDampingRatio )
		.function( "setMaxTorque", &ParallelJoint::setMaxTorque );

	class_<FilterJoint, base<Joint>>( "FilterJoint" );
}
