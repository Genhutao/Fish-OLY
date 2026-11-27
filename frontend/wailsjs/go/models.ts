export namespace main {
	
	export class AppSettings {
	    apiEndpoint: string;
	    apiKey: string;
	    model: string;
	    maxTokens: number;
	    temperature: number;
	    topP: number;
	    presencePenalty: number;
	    frequencyPenalty: number;
	    replyLength: string;
	    memoryTopK: number;
	    memoryWeightSim: number;
	    memoryWeightImp: number;
	    memoryWeightRec: number;
	    roamAutoPost: number;
	    roamRestThreshold: number;
	    roamEnergyDecay: number;
	    uiDensity: string;
	    reduceMotion: boolean;
	    typewriter: boolean;
	    enterToSend: boolean;
	    showTimestamps: boolean;
	    sceneBackground: boolean;
	    theme: string;
	    githubToken: string;
	
	    static createFrom(source: any = {}) {
	        return new AppSettings(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.apiEndpoint = source["apiEndpoint"];
	        this.apiKey = source["apiKey"];
	        this.model = source["model"];
	        this.maxTokens = source["maxTokens"];
	        this.temperature = source["temperature"];
	        this.topP = source["topP"];
	        this.presencePenalty = source["presencePenalty"];
	        this.frequencyPenalty = source["frequencyPenalty"];
	        this.replyLength = source["replyLength"];
	        this.memoryTopK = source["memoryTopK"];
	        this.memoryWeightSim = source["memoryWeightSim"];
	        this.memoryWeightImp = source["memoryWeightImp"];
	        this.memoryWeightRec = source["memoryWeightRec"];
	        this.roamAutoPost = source["roamAutoPost"];
	        this.roamRestThreshold = source["roamRestThreshold"];
	        this.roamEnergyDecay = source["roamEnergyDecay"];
	        this.uiDensity = source["uiDensity"];
	        this.reduceMotion = source["reduceMotion"];
	        this.typewriter = source["typewriter"];
	        this.enterToSend = source["enterToSend"];
	        this.showTimestamps = source["showTimestamps"];
	        this.sceneBackground = source["sceneBackground"];
	        this.theme = source["theme"];
	        this.githubToken = source["githubToken"];
	    }
	}
	export class AppStats {
	    personas: number;
	    memories: number;
	    posts: number;
	    stories: number;
	    skills: number;
	    presets: number;
	    roaming: number;
	    avgAffection: number;
	    ticks: number;
	
	    static createFrom(source: any = {}) {
	        return new AppStats(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.personas = source["personas"];
	        this.memories = source["memories"];
	        this.posts = source["posts"];
	        this.stories = source["stories"];
	        this.skills = source["skills"];
	        this.presets = source["presets"];
	        this.roaming = source["roaming"];
	        this.avgAffection = source["avgAffection"];
	        this.ticks = source["ticks"];
	    }
	}
	export class ChatMessage {
	    id: string;
	    sessionId: string;
	    role: string;
	    personaId?: string;
	    personaName?: string;
	    content: string;
	    strategy?: string;
	    kind?: string;
	    meta?: string;
	    mock?: boolean;
	    // Go type: time
	    createdAt: any;
	
	    static createFrom(source: any = {}) {
	        return new ChatMessage(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.sessionId = source["sessionId"];
	        this.role = source["role"];
	        this.personaId = source["personaId"];
	        this.personaName = source["personaName"];
	        this.content = source["content"];
	        this.strategy = source["strategy"];
	        this.kind = source["kind"];
	        this.meta = source["meta"];
	        this.mock = source["mock"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class ChatRequest {
	    personaId: string;
	    message: string;
	
	    static createFrom(source: any = {}) {
	        return new ChatRequest(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.personaId = source["personaId"];
	        this.message = source["message"];
	    }
	}
	export class MemoryEntry {
	    id: string;
	    personaId: string;
	    content: string;
	    vector: number[];
	    tags: string[];
	    important: number;
	    // Go type: time
	    createdAt: any;
	
	    static createFrom(source: any = {}) {
	        return new MemoryEntry(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.personaId = source["personaId"];
	        this.content = source["content"];
	        this.vector = source["vector"];
	        this.tags = source["tags"];
	        this.important = source["important"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class Persona {
	    id: string;
	    name: string;
	    avatar: string;
	    description: string;
	    personality: string;
	    coreAnchor: string;
	    tags: string[];
	    // Go type: time
	    createdAt: any;
	    // Go type: time
	    updatedAt: any;
	    affection: number;
	    mood: string;
	    evoVersion: number;
	    presetId: string;
	    skillIds: string[];
	
	    static createFrom(source: any = {}) {
	        return new Persona(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.name = source["name"];
	        this.avatar = source["avatar"];
	        this.description = source["description"];
	        this.personality = source["personality"];
	        this.coreAnchor = source["coreAnchor"];
	        this.tags = source["tags"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	        this.updatedAt = this.convertValues(source["updatedAt"], null);
	        this.affection = source["affection"];
	        this.mood = source["mood"];
	        this.evoVersion = source["evoVersion"];
	        this.presetId = source["presetId"];
	        this.skillIds = source["skillIds"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class MemorySnapshot {
	    id: string;
	    personaId: string;
	    version: number;
	    reason: string;
	    state: Persona;
	    // Go type: time
	    createdAt: any;
	
	    static createFrom(source: any = {}) {
	        return new MemorySnapshot(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.personaId = source["personaId"];
	        this.version = source["version"];
	        this.reason = source["reason"];
	        this.state = this.convertValues(source["state"], Persona);
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class OCComment {
	    id: string;
	    authorId: string;
	    author: string;
	    avatar: string;
	    content: string;
	    // Go type: time
	    createdAt: any;
	    isAI: boolean;
	
	    static createFrom(source: any = {}) {
	        return new OCComment(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.authorId = source["authorId"];
	        this.author = source["author"];
	        this.avatar = source["avatar"];
	        this.content = source["content"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	        this.isAI = source["isAI"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class OCPost {
	    id: string;
	    authorId: string;
	    author: string;
	    avatar: string;
	    content: string;
	    tags: string[];
	    likes: number;
	    comments: OCComment[];
	    // Go type: time
	    createdAt: any;
	    isAI: boolean;
	
	    static createFrom(source: any = {}) {
	        return new OCPost(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.authorId = source["authorId"];
	        this.author = source["author"];
	        this.avatar = source["avatar"];
	        this.content = source["content"];
	        this.tags = source["tags"];
	        this.likes = source["likes"];
	        this.comments = this.convertValues(source["comments"], OCComment);
	        this.createdAt = this.convertValues(source["createdAt"], null);
	        this.isAI = source["isAI"];
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	
	export class StorySession {
	    id: string;
	    title: string;
	    description: string;
	    personaIds: string[];
	    userRole: string;
	    messages: ChatMessage[];
	    // Go type: time
	    createdAt: any;
	
	    static createFrom(source: any = {}) {
	        return new StorySession(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.title = source["title"];
	        this.description = source["description"];
	        this.personaIds = source["personaIds"];
	        this.userRole = source["userRole"];
	        this.messages = this.convertValues(source["messages"], ChatMessage);
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }
	
		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}

	export class Skill {
	    id: string;
	    name: string;
	    icon: string;
	    category: string;
	    description: string;
	    prompt: string;
	    source?: string;
	    builtin: boolean;
	    // Go type: time
	    createdAt: any;

	    static createFrom(source: any = {}) {
	        return new Skill(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.name = source["name"];
	        this.icon = source["icon"];
	        this.category = source["category"];
	        this.description = source["description"];
	        this.prompt = source["prompt"];
	        this.source = source["source"];
	        this.builtin = source["builtin"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class Preset {
	    id: string;
	    name: string;
	    icon: string;
	    description: string;
	    scenario: string;
	    style: string;
	    systemSnippet: string;
	    tags: string[];
	    builtin: boolean;
	    // Go type: time
	    createdAt: any;

	    static createFrom(source: any = {}) {
	        return new Preset(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.name = source["name"];
	        this.icon = source["icon"];
	        this.description = source["description"];
	        this.scenario = source["scenario"];
	        this.style = source["style"];
	        this.systemSnippet = source["systemSnippet"];
	        this.tags = source["tags"];
	        this.builtin = source["builtin"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class RoamScene {
	    id: string;
	    name: string;
	    icon: string;
	    description: string;
	    ambience: string;

	    static createFrom(source: any = {}) {
	        return new RoamScene(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.name = source["name"];
	        this.icon = source["icon"];
	        this.description = source["description"];
	        this.ambience = source["ambience"];
	    }
	}
	export class RoamEvent {
	    id: string;
	    personaId: string;
	    personaName: string;
	    avatar: string;
	    sceneId: string;
	    sceneName: string;
	    kind: string;
	    content: string;
	    // Go type: time
	    createdAt: any;

	    static createFrom(source: any = {}) {
	        return new RoamEvent(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.personaId = source["personaId"];
	        this.personaName = source["personaName"];
	        this.avatar = source["avatar"];
	        this.sceneId = source["sceneId"];
	        this.sceneName = source["sceneName"];
	        this.kind = source["kind"];
	        this.content = source["content"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class RoamPersona {
	    personaId: string;
	    name: string;
	    avatar: string;
	    mood: string;
	    affection: number;
	    sceneId: string;
	    sceneName: string;
	    status: string;
	    activity: string;
	    energy: number;

	    static createFrom(source: any = {}) {
	        return new RoamPersona(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.personaId = source["personaId"];
	        this.name = source["name"];
	        this.avatar = source["avatar"];
	        this.mood = source["mood"];
	        this.affection = source["affection"];
	        this.sceneId = source["sceneId"];
	        this.sceneName = source["sceneName"];
	        this.status = source["status"];
	        this.activity = source["activity"];
	        this.energy = source["energy"];
	    }
	}
	export class RoamView {
	    running: boolean;
	    interval: number;
	    tick: number;
	    scenes: RoamScene[];
	    personas: RoamPersona[];
	    events: RoamEvent[];

	    static createFrom(source: any = {}) {
	        return new RoamView(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.running = source["running"];
	        this.interval = source["interval"];
	        this.tick = source["tick"];
	        this.scenes = this.convertValues(source["scenes"], RoamScene);
	        this.personas = this.convertValues(source["personas"], RoamPersona);
	        this.events = this.convertValues(source["events"], RoamEvent);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) {
		        return a;
		    }
		    if (a.slice && a.map) {
		        return (a as any[]).map(elem => this.convertValues(elem, classs));
		    } else if ("object" === typeof a) {
		        if (asMap) {
		            for (const key of Object.keys(a)) {
		                a[key] = new classs(a[key]);
		            }
		            return a;
		        }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class Group {
	    id: string;
	    name: string;
	    icon: string;
	    kind: string;
	    sceneId: string;
	    topic: string;
	    memberIds: string[];
	    messages: ChatMessage[];
	    live: boolean;
	    // Go type: time
	    createdAt: any;

	    static createFrom(source: any = {}) {
	        return new Group(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.id = source["id"];
	        this.name = source["name"];
	        this.icon = source["icon"];
	        this.kind = source["kind"];
	        this.sceneId = source["sceneId"];
	        this.topic = source["topic"];
	        this.memberIds = source["memberIds"];
	        this.messages = this.convertValues(source["messages"], ChatMessage);
	        this.live = source["live"];
	        this.createdAt = this.convertValues(source["createdAt"], null);
	    }

		convertValues(a: any, classs: any, asMap: boolean = false): any {
		    if (!a) return a;
		    if (a.slice && a.map) return (a as any[]).map(elem => this.convertValues(elem, classs));
		    if ("object" === typeof a) {
		        if (asMap) { for (const key of Object.keys(a)) a[key] = new classs(a[key]); return a; }
		        return new classs(a);
		    }
		    return a;
		}
	}
	export class GitHubSkill {
	    fullName: string;
	    name: string;
	    description: string;
	    url: string;
	    stars: number;
	    icon: string;

	    static createFrom(source: any = {}) {
	        return new GitHubSkill(source);
	    }

	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.fullName = source["fullName"];
	        this.name = source["name"];
	        this.description = source["description"];
	        this.url = source["url"];
	        this.stars = source["stars"];
	        this.icon = source["icon"];
	    }
	}

}
