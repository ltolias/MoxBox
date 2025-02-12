import _ from 'underscore';

const url_prefix = 'https://'

export default (state, setState, server_ip) => ({
    
    fetch: (path) => fetch(`${url_prefix}${server_ip}/${path}`),

    device_to_state: async (name) => {
        const known = localStorage.getItem(name);
        if (_.size(known) > 0) {
            setState((prevState) => ({ ...prevState, [name]: JSON.parse(known) }));
        }
        
        const response = await fetch(`${url_prefix}${server_ip}/${name}`);
        if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);

        let result;
        const contentType = response.headers.get('content-type');
        if (contentType && contentType.startsWith('application/json')) {
            result = await response.json();
            localStorage.setItem(name, JSON.stringify(result));
        } else {
            result = await response.text();
        }
        setState((prevState) => ({ ...prevState, [name]: result }));
    },

    state_to_device: async (name, json) => {
        const response = await fetch(`${url_prefix}${server_ip}/${name}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(json),
        });
        if (!response.ok) throw new Error(`HTTP error! status: ${response.status}`);
    },

    toggle_looper: (task, loop_enabled_ref) => async (event) => {
        if (event.target.checked) { 
            loop_enabled_ref.current = true;
            const looper = async () => {
                if (!loop_enabled_ref.current) {
                    return
                }
                try {
                    await task()
                } catch(err) {}
                setTimeout(looper, loop_enabled_ref.period)
            }
            looper();  
        } else {
            loop_enabled_ref.current = false
        }    
    },

    config_changer: (param) => (e) => {
        var { value } = e.target;
        if (e.target.type == 'number') { value = Number(value)}
        setState((prevState) => ({...prevState, config: {...prevState.config, [param]: value}}))
    }
})