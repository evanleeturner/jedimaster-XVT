#include "xvt/render/render_clip.h"

#include "xvt/render/render_scene.h"
#include "xvt/render/renderer.h"

// GLOBAL: XVT 0x53F8F0
int g_clipIdxA[32];
// GLOBAL: XVT 0x52F870
int g_clipIdxB[32] = { 0 };
// GLOBAL: XVT 0x54F978
int g_clipCountA;
// GLOBAL: XVT 0x54F998
int g_clipCountB = 0;
// GLOBAL: XVT 0x54F99C
int g_clipVertCursor;
// GLOBAL: XVT 0x99940C
float g_invProjScale;

// FLAGS: /O2 /G5
// FUNCTION: XVT 0x4096D0
int RenderClip_ClipPolyTop(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices) {
	RenderClipVertex* previous;
	RenderClipVertex* current;
	float previousY;
	float currentY;
	int result;
	previous = &vertices[prevVertIndex];
	previousY = previous->y;
	current = &vertices[curVertIndex];
	currentY = current->y;
	result = INT32_MIN;

	if (previousY < 0.0f) {
		if (currentY >= 0.0f) {
			int output = g_clipVertCursor++;
			float previousX = previous->x;
			float currentX = current->x;
			float previousRhw = previous->rhw;
			float currentRhw = current->rhw;
			float previousU = previous->u;
			float currentU = current->u;
			float previousV = previous->v;
			float currentV = current->v;
			float previousZ = previous->z;
			float currentZ = current->z;
			float deltaU = currentU - previousU;
			float deltaX = currentX - previousX;
			float deltaRhw = currentRhw - previousRhw;
			float deltaY = currentY - previousY;
			float deltaV = currentV - previousV;
			float deltaZ = currentZ - previousZ;
			RenderClipVertex* destination;
			float t;

			previousY = -previousY;
			if (previousY < currentY) {
				t = previousY / deltaY;
				destination = &vertices[output];
				destination->x = previousX + t * deltaX;
				destination->rhw = previousRhw + t * deltaRhw;
				destination->z = previousZ + t * deltaZ;
				if (deltaZ != 0.0f) {
					float baseProjection = (float)(unsigned int)g_projScaleInt / previousZ;
					float otherProjection = (float)(unsigned int)g_projScaleInt / currentZ;
					float destinationProjection = (float)(unsigned int)g_projScaleInt / destination->z;
					float uvT = (destinationProjection - baseProjection) / (otherProjection - baseProjection);
					destination->u = previousU + deltaU * uvT;
					destination->v = previousV + deltaV * uvT;
				} else {
					destination->u = previousU + t * deltaU;
					destination->v = previousV + t * deltaV;
				}
			} else {
				t = currentY / deltaY;
				destination = &vertices[output];
				destination->x = currentX - deltaX * t;
				destination->rhw = currentRhw - deltaRhw * t;
				destination->z = currentZ - deltaZ * t;
				if (deltaZ != 0.0f) {
					float baseProjection = (float)(unsigned int)g_projScaleInt / currentZ;
					float otherProjection = (float)(unsigned int)g_projScaleInt / previousZ;
					float destinationProjection = (float)(unsigned int)g_projScaleInt / destination->z;
					float uvT = (destinationProjection - baseProjection) / (otherProjection - baseProjection);
					destination->u = currentU - deltaU * uvT;
					destination->v = currentV - deltaV * uvT;
				} else {
					destination->u = currentU - deltaU * t;
					destination->v = currentV - deltaV * t;
				}
			}
			destination->y = 0.0f;
			g_clipIdxB[g_clipCountB++] = output;
			g_clipIdxB[g_clipCountB++] = curVertIndex;
			return g_clipCountB;
		}
	} else if (currentY < 0.0f) {
		int output = g_clipVertCursor++;
		float previousX = previous->x;
		float currentX = current->x;
		float previousRhw = previous->rhw;
		float currentRhw = current->rhw;
		float previousU = previous->u;
		float currentU = current->u;
		float previousV = previous->v;
		float currentV = current->v;
		float previousZ = previous->z;
		float currentZ = current->z;
		float deltaX = currentX - previousX;
		float deltaY = currentY - previousY;
		float deltaRhw = currentRhw - previousRhw;
		float deltaU = currentU - previousU;
		float deltaV = currentV - previousV;
		float deltaZ = currentZ - previousZ;
		RenderClipVertex* destination;
		float t;

		currentY = -currentY;
		if (currentY > previousY) {
			t = previousY / deltaY;
			destination = &vertices[output];
			destination->x = previousX - t * deltaX;
			destination->rhw = previousRhw - t * deltaRhw;
			destination->z = previousZ - t * deltaZ;
			if (deltaZ != 0.0f) {
				float baseProjection = (float)(unsigned int)g_projScaleInt / previousZ;
				float otherProjection = (float)(unsigned int)g_projScaleInt / currentZ;
				float destinationProjection = (float)(unsigned int)g_projScaleInt / destination->z;
				float uvT = (destinationProjection - baseProjection) / (otherProjection - baseProjection);
				destination->u = previousU + deltaU * uvT;
				destination->v = previousV + deltaV * uvT;
			} else {
				destination->u = previousU - t * deltaU;
				destination->v = previousV - t * deltaV;
			}
		} else {
			t = currentY / deltaY;
			destination = &vertices[output];
			destination->x = currentX + deltaX * t;
			destination->rhw = currentRhw + deltaRhw * t;
			destination->z = currentZ + deltaZ * t;
			if (deltaZ != 0.0f) {
				float baseProjection = (float)(unsigned int)g_projScaleInt / currentZ;
				float otherProjection = (float)(unsigned int)g_projScaleInt / previousZ;
				float destinationProjection = (float)(unsigned int)g_projScaleInt / destination->z;
				float uvT = (destinationProjection - baseProjection) / (otherProjection - baseProjection);
				destination->u = currentU - deltaU * uvT;
				destination->v = currentV - deltaV * uvT;
			} else {
				destination->u = currentU + deltaU * t;
				destination->v = currentV + deltaV * t;
			}
		}
		destination->y = 0.0f;
		g_clipIdxB[g_clipCountB++] = output;
		return g_clipCountB;
	} else {
		g_clipIdxB[g_clipCountB++] = curVertIndex;
		result = g_clipCountB;
	}
	return result;
}

// FUNCTION: XVT 0x409C10
void RenderClip_ClipPolyBottom(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices) {
	RenderClipVertex* previous = &vertices[prevVertIndex];
	float previousY = previous->y;
	RenderClipVertex* current = &vertices[curVertIndex];
	float currentY = current->y;
	int viewportBottom = g_flightVpMaxY;
	float boundary = (float)viewportBottom;

	if (previousY > boundary) {
		if (currentY >= boundary)
			return;

		{
			int output;
			float previousX, currentX, previousRhw, currentRhw;
			float previousU, currentU, previousV, currentV;
			float previousZ, currentZ;
			float deltaX, deltaY, deltaRhw, deltaU, deltaV, deltaZ;

			output = g_clipVertCursor++;
			previousX = previous->x;
			currentX = current->x;
			previousRhw = previous->rhw;
			currentRhw = current->rhw;
			previousU = previous->u;
			currentU = current->u;
			previousV = previous->v;
			currentV = current->v;
			previousZ = previous->z;
			currentZ = current->z;
			deltaX = currentX - previousX;
			deltaY = currentY - previousY;
			deltaRhw = currentRhw - previousRhw;
			deltaU = currentU - previousU;
			deltaV = currentV - previousV;
			deltaZ = currentZ - previousZ;

			previousY = previousY - boundary;
			currentY = boundary - currentY;
			if (currentY > previousY) {
				previousY = previousY / deltaY;
				vertices[output].x = previousX - previousY * deltaX;
				vertices[output].rhw = previousRhw - previousY * deltaRhw;
				vertices[output].z = previousZ - previousY * deltaZ;
				if (deltaZ != 0.0f) {
					float projection = (float)g_projScaleInt;
					float previousProjection = projection / previousZ;
					float currentProjection = projection / currentZ;
					float destinationProjection = projection / vertices[output].z;
					float uvT = (destinationProjection - previousProjection) /
								(currentProjection - previousProjection);
					vertices[output].u = previousU + deltaU * uvT;
					vertices[output].v = previousV + deltaV * uvT;
				} else {
					vertices[output].u = previousU - previousY * deltaU;
					vertices[output].v = previousV - previousY * deltaV;
				}
			} else {
				currentY = currentY / deltaY;
				vertices[output].x = currentX + currentY * deltaX;
				vertices[output].rhw = currentRhw + deltaRhw * currentY;
				vertices[output].z = currentZ + deltaZ * currentY;
				if (deltaZ != 0.0f) {
					float projection = (float)g_projScaleInt;
					float previousProjection = projection / previousZ;
					float currentProjection = projection / currentZ;
					float destinationProjection = projection / vertices[output].z;
					float uvT = (destinationProjection - currentProjection) /
								(previousProjection - currentProjection);
					vertices[output].u = currentU - deltaU * uvT;
					vertices[output].v = currentV - deltaV * uvT;
				} else {
					vertices[output].u = currentU + deltaU * currentY;
					vertices[output].v = currentV + deltaV * currentY;
				}
			}
			vertices[output].y = (float)g_flightVpMaxY;
			g_clipIdxA[g_clipCountA++] = output;
			g_clipIdxA[g_clipCountA++] = curVertIndex;
		}
	} else if (currentY > boundary) {
		int output;
		float previousX, currentX, previousRhw, currentRhw;
		float previousU, currentU, previousV, currentV;
		float previousZ, currentZ;
		float deltaX, deltaY, deltaRhw, deltaU, deltaV, deltaZ;

		output = g_clipVertCursor++;
		previousRhw = previous->rhw;
		previousX = previous->x;
		currentX = current->x;
		currentRhw = current->rhw;
		previousU = previous->u;
		currentU = current->u;
		previousV = previous->v;
		currentV = current->v;
		previousZ = previous->z;
		currentZ = current->z;
		deltaX = currentX - previousX;
		deltaY = currentY - previousY;
		deltaRhw = currentRhw - previousRhw;
		deltaU = currentU - previousU;
		deltaV = currentV - previousV;
		deltaZ = currentZ - previousZ;

		currentY = currentY - boundary;
		previousY = boundary - previousY;
		if (currentY > previousY) {
			previousY = previousY / deltaY;
			vertices[output].x = previousX + previousY * deltaX;
			vertices[output].rhw = previousRhw + previousY * deltaRhw;
			vertices[output].z = previousZ + previousY * deltaZ;
			if (deltaZ != 0.0f) {
				float projection = (float)g_projScaleInt;
				float previousProjection = projection / previousZ;
				float currentProjection = projection / currentZ;
				float destinationProjection = projection / vertices[output].z;
				float uvT =
					(destinationProjection - previousProjection) / (currentProjection - previousProjection);
				vertices[output].u = previousU + deltaU * uvT;
				vertices[output].v = previousV + deltaV * uvT;
			} else {
				vertices[output].u = previousU + previousY * deltaU;
				vertices[output].v = previousV + previousY * deltaV;
			}
		} else {
			currentY = currentY / deltaY;
			vertices[output].x = currentX - currentY * deltaX;
			vertices[output].rhw = currentRhw - deltaRhw * currentY;
			vertices[output].z = currentZ - deltaZ * currentY;
			if (deltaZ != 0.0f) {
				float projection = (float)g_projScaleInt;
				float previousProjection = projection / previousZ;
				float currentProjection = projection / currentZ;
				float destinationProjection = projection / vertices[output].z;
				float uvT =
					(destinationProjection - currentProjection) / (previousProjection - currentProjection);
				vertices[output].u = currentU - deltaU * uvT;
				vertices[output].v = currentV - deltaV * uvT;
			} else {
				vertices[output].u = currentU - deltaU * currentY;
				vertices[output].v = currentV - deltaV * currentY;
			}
		}
		vertices[output].y = (float)g_flightVpMaxY;
		g_clipIdxA[g_clipCountA++] = output;
	} else {
		g_clipIdxA[g_clipCountA++] = curVertIndex;
	}
}

// FUNCTION: XVT 0x40A1A0
int RenderClip_ClipPolyLeft(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices) {
	RenderClipVertex* previous;
	RenderClipVertex* current;
	float currentX;
	float previousX;
	int result;
	previous = &vertices[prevVertIndex];
	previousX = previous->x;
	current = &vertices[curVertIndex];
	currentX = current->x;
	result = INT32_MIN;

	if (previousX < 0.0f) {
		if (currentX < 0.0f)
			return result;

		{
			int output;
			float currentY, previousY, currentRhw, previousRhw;
			float currentU, previousU, currentV, previousV;
			float currentZ, previousZ, deltaX, deltaY, deltaRhw;
			float deltaU, deltaV, deltaZ;
			RenderClipVertex* destination;
			output = g_clipVertCursor++;
			previousY = previous->y;
			currentY = current->y;
			previousRhw = previous->rhw;
			currentRhw = current->rhw;
			previousU = previous->u;
			currentU = current->u;
			previousV = previous->v;
			currentV = current->v;
			previousZ = previous->z;
			currentZ = current->z;
			deltaX = currentX - previousX;
			deltaY = currentY - previousY;
			deltaRhw = currentRhw - previousRhw;
			deltaU = currentU - previousU;
			deltaV = currentV - previousV;
			deltaZ = currentZ - previousZ;

			previousX = -previousX;
			if (previousX < currentX) {
				previousX = previousX / deltaX;
				destination = &vertices[output];
				destination->y = previousY + previousX * deltaY;
				destination->rhw = previousRhw + previousX * deltaRhw;
				destination->z = previousZ + previousX * deltaZ;
				if (deltaZ != 0.0f) {
					float projection;
					float baseProjection;
					float otherProjection;
					float uvT;
					projection = (float)(unsigned int)g_projScaleInt;
					baseProjection = projection / previousZ;
					otherProjection = projection / currentZ;
					uvT = (projection / destination->z - baseProjection) / (otherProjection - baseProjection);
					destination->u = previousU + deltaU * uvT;
					destination->v = previousV + deltaV * uvT;
				} else {
					destination->u = previousU + previousX * deltaU;
					destination->v = previousV + previousX * deltaV;
				}
			} else {
				currentX = currentX / deltaX;
				destination = &vertices[output];
				destination->y = currentY - currentX * deltaY;
				destination->rhw = currentRhw - currentX * deltaRhw;
				destination->z = currentZ - currentX * deltaZ;
				if (deltaZ != 0.0f) {
					float projection;
					float baseProjection;
					float otherProjection;
					float uvT;
					projection = (float)(unsigned int)g_projScaleInt;
					baseProjection = projection / currentZ;
					otherProjection = projection / previousZ;
					uvT = (projection / destination->z - baseProjection) / (otherProjection - baseProjection);
					destination->u = currentU - deltaU * uvT;
					destination->v = currentV - deltaV * uvT;
				} else {
					destination->u = currentU - currentX * deltaU;
					destination->v = currentV - currentX * deltaV;
				}
			}
			destination->x = 0.0f;
			g_clipIdxB[g_clipCountB++] = output;
			g_clipIdxB[g_clipCountB++] = curVertIndex;
			return g_clipCountB;
		}
	} else if (currentX < 0.0f) {
		{
			int output;
			float currentY, previousY, currentRhw, previousRhw;
			float currentU, previousU, currentV, previousV;
			float currentZ, previousZ, deltaX, deltaY, deltaRhw;
			float deltaU, deltaV, deltaZ;
			RenderClipVertex* destination;
			output = g_clipVertCursor++;
			previousY = previous->y;
			currentY = current->y;
			previousRhw = previous->rhw;
			currentRhw = current->rhw;
			previousU = previous->u;
			currentU = current->u;
			previousV = previous->v;
			currentV = current->v;
			previousZ = previous->z;
			currentZ = current->z;
			deltaX = currentX - previousX;
			deltaY = currentY - previousY;
			deltaRhw = currentRhw - previousRhw;
			deltaU = currentU - previousU;
			deltaV = currentV - previousV;
			deltaZ = currentZ - previousZ;

			currentX = -currentX;
			if (currentX > previousX) {
				previousX = previousX / deltaX;
				destination = &vertices[output];
				destination->y = previousY - previousX * deltaY;
				destination->rhw = previousRhw - previousX * deltaRhw;
				destination->z = previousZ - previousX * deltaZ;
				if (deltaZ != 0.0f) {
					float projection;
					float baseProjection;
					float otherProjection;
					float uvT;
					projection = (float)(unsigned int)g_projScaleInt;
					baseProjection = projection / previousZ;
					otherProjection = projection / currentZ;
					uvT = (projection / destination->z - baseProjection) / (otherProjection - baseProjection);
					destination->u = previousU + deltaU * uvT;
					destination->v = previousV + deltaV * uvT;
				} else {
					destination->u = previousU - previousX * deltaU;
					destination->v = previousV - previousX * deltaV;
				}
			} else {
				currentX = currentX / deltaX;
				destination = &vertices[output];
				destination->y = currentY + currentX * deltaY;
				destination->rhw = currentRhw + currentX * deltaRhw;
				destination->z = currentZ + currentX * deltaZ;
				if (deltaZ != 0.0f) {
					float projection;
					float baseProjection;
					float otherProjection;
					float uvT;
					projection = (float)(unsigned int)g_projScaleInt;
					baseProjection = projection / currentZ;
					otherProjection = projection / previousZ;
					uvT = (projection / destination->z - baseProjection) / (otherProjection - baseProjection);
					destination->u = currentU - deltaU * uvT;
					destination->v = currentV - deltaV * uvT;
				} else {
					destination->u = currentU + currentX * deltaU;
					destination->v = currentV + currentX * deltaV;
				}
			}
			destination->x = 0.0f;
			g_clipIdxB[g_clipCountB++] = output;
			return g_clipCountB;
		}
	} else {
		g_clipIdxB[g_clipCountB++] = curVertIndex;
		result = g_clipCountB;
	}
	return result;
}

// FUNCTION: XVT 0x40A6E0
void RenderClip_ClipPolyRight(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices) {
	RenderClipVertex* previous = &vertices[prevVertIndex];
	float previousX = previous->x;
	RenderClipVertex* current = &vertices[curVertIndex];
	float currentX = current->x;
	int viewportWidth = g_flightVpWidth;
	float boundary = (float)viewportWidth;

	if (previousX > boundary) {
		if (currentX > boundary)
			return;

		{
			int output;
			float previousY, currentY, previousRhw, currentRhw;
			float previousU, currentU, previousV, currentV;
			float previousZ, currentZ;
			float deltaX, deltaY, deltaRhw, deltaU, deltaV, deltaZ;

			output = g_clipVertCursor++;
			previousY = previous->y;
			currentY = current->y;
			previousRhw = previous->rhw;
			currentRhw = current->rhw;
			previousU = previous->u;
			currentU = current->u;
			previousV = previous->v;
			currentV = current->v;
			previousZ = previous->z;
			currentZ = current->z;
			deltaX = currentX - previousX;
			deltaY = currentY - previousY;
			deltaRhw = currentRhw - previousRhw;
			deltaU = currentU - previousU;
			deltaV = currentV - previousV;
			deltaZ = currentZ - previousZ;

			previousX = previousX - boundary;
			currentX = boundary - currentX;
			if (currentX > previousX) {
				previousX = previousX / deltaX;
				vertices[output].y = previousY - previousX * deltaY;
				vertices[output].rhw = previousRhw - previousX * deltaRhw;
				vertices[output].z = previousZ - previousX * deltaZ;
				if (deltaZ != 0.0f) {
					float projection = (float)g_projScaleInt;
					float previousProjection = projection / previousZ;
					float currentProjection = projection / currentZ;
					float destinationProjection = projection / vertices[output].z;
					float uvT = (destinationProjection - previousProjection) /
								(currentProjection - previousProjection);
					vertices[output].u = previousU + deltaU * uvT;
					vertices[output].v = previousV + deltaV * uvT;
				} else {
					vertices[output].u = previousU - previousX * deltaU;
					vertices[output].v = previousV - previousX * deltaV;
				}
			} else {
				currentX = currentX / deltaX;
				vertices[output].y = currentY + currentX * deltaY;
				vertices[output].rhw = currentRhw + deltaRhw * currentX;
				vertices[output].z = currentZ + deltaZ * currentX;
				if (deltaZ != 0.0f) {
					float projection = (float)g_projScaleInt;
					float previousProjection = projection / previousZ;
					float currentProjection = projection / currentZ;
					float destinationProjection = projection / vertices[output].z;
					float uvT = (destinationProjection - currentProjection) /
								(previousProjection - currentProjection);
					vertices[output].u = currentU - deltaU * uvT;
					vertices[output].v = currentV - deltaV * uvT;
				} else {
					vertices[output].u = currentU + deltaU * currentX;
					vertices[output].v = currentV + deltaV * currentX;
				}
			}
			vertices[output].x = (float)g_flightVpWidth;
			g_clipIdxA[g_clipCountA++] = output;
			g_clipIdxA[g_clipCountA++] = curVertIndex;
		}
	} else if (currentX > boundary) {
		int output;
		float previousY, currentY, previousRhw, currentRhw;
		float previousU, currentU, previousV, currentV;
		float previousZ, currentZ;
		float deltaX, deltaY, deltaRhw, deltaU, deltaV, deltaZ;

		output = g_clipVertCursor++;
		previousRhw = previous->rhw;
		previousY = previous->y;
		currentY = current->y;
		currentRhw = current->rhw;
		previousU = previous->u;
		currentU = current->u;
		previousV = previous->v;
		currentV = current->v;
		previousZ = previous->z;
		currentZ = current->z;
		deltaX = currentX - previousX;
		deltaY = currentY - previousY;
		deltaRhw = currentRhw - previousRhw;
		deltaU = currentU - previousU;
		deltaV = currentV - previousV;
		deltaZ = currentZ - previousZ;

		previousX = boundary - previousX;
		currentX = currentX - boundary;
		if (currentX > previousX) {
			previousX = previousX / deltaX;
			vertices[output].y = previousY + previousX * deltaY;
			vertices[output].rhw = previousRhw + previousX * deltaRhw;
			vertices[output].z = previousZ + previousX * deltaZ;
			if (deltaZ != 0.0f) {
				float projection = (float)g_projScaleInt;
				float previousProjection = projection / previousZ;
				float currentProjection = projection / currentZ;
				float destinationProjection = projection / vertices[output].z;
				float uvT =
					(destinationProjection - previousProjection) / (currentProjection - previousProjection);
				vertices[output].u = previousU + deltaU * uvT;
				vertices[output].v = previousV + deltaV * uvT;
			} else {
				vertices[output].u = previousU + previousX * deltaU;
				vertices[output].v = previousV + previousX * deltaV;
			}
		} else {
			currentX = currentX / deltaX;
			vertices[output].y = currentY - currentX * deltaY;
			vertices[output].rhw = currentRhw - deltaRhw * currentX;
			vertices[output].z = currentZ - deltaZ * currentX;
			if (deltaZ != 0.0f) {
				float projection = (float)g_projScaleInt;
				float previousProjection = projection / previousZ;
				float currentProjection = projection / currentZ;
				float destinationProjection = projection / vertices[output].z;
				float uvT =
					(destinationProjection - currentProjection) / (previousProjection - currentProjection);
				vertices[output].u = currentU - deltaU * uvT;
				vertices[output].v = currentV - deltaV * uvT;
			} else {
				vertices[output].u = currentU - deltaU * currentX;
				vertices[output].v = currentV - deltaV * currentX;
			}
		}
		vertices[output].x = (float)g_flightVpWidth;
		g_clipIdxA[g_clipCountA++] = output;
	} else {
		g_clipIdxA[g_clipCountA++] = curVertIndex;
	}
}

// FUNCTION: XVT 0x40AC80
void RenderClip_ClipPolyNear(int prevVertIndex, int curVertIndex, RenderClipVertex* vertices) {
	RenderClipVertex* previous = &vertices[prevVertIndex];
	float previousZ = previous->z;
	RenderClipVertex* current = &vertices[curVertIndex];
	float currentZ = current->z;
	int output;

	if (previousZ < 0.0f) {
		float previousX;
		float previousY;
		float previousRhw;
		float previousU;
		float previousV;
		float currentScale;
		float currentX;
		float currentY;
		float deltaU;
		float deltaV;
		float t;
		float projection;

		if (currentZ < 0.0f) {
			return;
		}
		output = g_clipVertCursor++;
		previousX = previous->x;
		previousY = previous->y;
		previousRhw = previous->rhw;
		previousU = previous->u;
		currentScale = (float)(unsigned int)g_projScaleInt / currentZ;
		previousV = previous->v;
		currentX = (current->x - (float)(g_flightVpWidth >> 1)) * currentScale;
		currentX = currentX * g_invProjScale - previousX;
		currentY = (current->y - (float)(g_projOffsetY + (g_flightVpHeight >> 1))) * currentScale;
		currentY = currentY * g_invProjScale - previousY;
		deltaU = current->u - previousU;
		deltaV = current->v - previousV;
		t = currentScale - previousZ;
		t = previousZ / (t - g_renderUnitFloat);
		vertices[output].rhw = previousRhw - (current->rhw - previousRhw) * t;
		vertices[output].x = previousX - currentX * t;
		vertices[output].y = previousY - currentY * t;
		vertices[output].v = previousV - t * deltaV;
		vertices[output].u = previousU - deltaU * t;
		projection = (float)(unsigned int)g_projScaleInt;
		vertices[output].z = projection;
		vertices[output].x = vertices[output].x * projection;
		vertices[output].y = projection * vertices[output].y;
		vertices[output].x = (float)(g_flightVpWidth >> 1) + vertices[output].x;
		vertices[output].y = (float)(g_projOffsetY + (g_flightVpHeight >> 1)) + vertices[output].y;
		g_clipIdxA[g_clipCountA++] = output;
		g_clipIdxA[g_clipCountA++] = curVertIndex;
	} else if (currentZ < 0.0f) {
		float currentX;
		float currentY;
		float currentRhw;
		float currentU;
		float currentV;
		float previousScale;
		float previousX;
		float previousY;
		float deltaU;
		float deltaV;
		float t;
		float projection;

		output = g_clipVertCursor++;
		currentX = current->x;
		previousScale = (float)(unsigned int)g_projScaleInt / previousZ;
		currentY = current->y;
		currentRhw = current->rhw;
		currentU = current->u;
		currentV = current->v;
		previousX = (previous->x - (float)(g_flightVpWidth >> 1)) * previousScale;
		previousX = currentX - previousX * g_invProjScale;
		previousY = (previous->y - (float)(g_projOffsetY + (g_flightVpHeight >> 1))) * previousScale;
		previousY = currentY - previousY * g_invProjScale;
		deltaU = currentU - previous->u;
		deltaV = currentV - previous->v;
		t = currentZ / (currentZ - previousScale + g_renderUnitFloat);
		vertices[output].rhw = currentRhw - (currentRhw - previous->rhw) * t;
		vertices[output].x = currentX - previousX * t;
		vertices[output].y = currentY - previousY * t;
		vertices[output].u = currentU - deltaU * t;
		vertices[output].v = currentV - t * deltaV;
		projection = (float)(unsigned int)g_projScaleInt;
		vertices[output].z = projection;
		vertices[output].x = vertices[output].x * projection;
		vertices[output].y = projection * vertices[output].y;
		vertices[output].x = (float)(g_flightVpWidth >> 1) + vertices[output].x;
		vertices[output].y = (float)(g_projOffsetY + (g_flightVpHeight >> 1)) + vertices[output].y;
		g_clipIdxA[g_clipCountA++] = output;
	} else {
		g_clipIdxA[g_clipCountA++] = curVertIndex;
	}
}
